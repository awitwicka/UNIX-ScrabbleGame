#define _GNU_SOURCE 
#include "common.h"

#define T_SIZE 25
#define B_SIZE 5
#define TILE_START 5

//volatile sig_atomic_t do_work=1;

void sigint_handler(int sig) {
	//do_work = 0;
}

void usage(char * name){
	fprintf(stderr,"USAGE: %s domain port\n",name);
}

int check_if_opponent_connected(int input) {
	if(input == -1) {
		fprintf(stderr, "Opponent disconnected. You won!\n");	
		return 0;	
	} 
	return 1;
}

void print_board(char board[B_SIZE][B_SIZE]) {
	fprintf(stdout, "\nCURRENT BOARD:\n");
	int i,j,k;
	fprintf(stdout, "  ");
	for (i=0; i<B_SIZE; i++)
		fprintf(stdout, "  %i ", i);
	fprintf(stdout, "\n  ");
	for (i=0; i<B_SIZE; i++)
		fprintf(stdout, "+---");
	fprintf(stdout, "+\n");
	for (i=0; i<B_SIZE; i++) {
		fprintf(stdout, "%i ", i);
		for (j=0; j<B_SIZE; j++) {
			fprintf(stdout, "| ");
			if (board[i][j]!=0)
				fprintf(stdout, "%c ", board[i][j]);
			else
				fprintf(stdout, "  ");	
		}
		fprintf(stdout, "|\n  ");
		for (k=0; k<B_SIZE; k++)
			fprintf(stdout, "+---");
		fprintf(stdout, "+\n");
	}
}

void print_tiles(char tiles[TILE_START]) {
	int i;
	fprintf(stdout, "Your letters: ");
	for(i=0; i<TILE_START; i++) {
		fprintf(stdout, "%c ", tiles[i]);
	}
	fprintf(stdout, "\n");
}

char receive_letter(int fd) {
	char letter;
	int32_t l;
	if(bulk_read(fd,(char *)&l,sizeof(int32_t))<(int)sizeof(int32_t)) ERR("read");
	letter = ntohl(l);
	return letter;
}

void send_move(int fd, int x, int y, char letter) {
	int32_t move[3] = {htonl(x), htonl(y), htonl(letter)};
	if(bulk_write(fd,(char *)move,sizeof(int32_t[3]))<0) ERR("write:");
}

int receive_move(int fd, int* x, int* y, char* letter) {
	int32_t move[3];
	memset(move,0,sizeof(move));
	if(bulk_read(fd,(char *)move,sizeof(int32_t[3]))<(int)sizeof(int32_t[3])) ERR("read:");
	*x = ntohl(move[0]);
	*y = ntohl(move[1]);
	*letter = ntohl(move[2]);
	return check_if_opponent_connected(ntohl(move[0]));
}

int move_validation(char board[B_SIZE][B_SIZE], char tiles[TILE_START], int x, int y, char letter, int move_no) {
	//TODO: male duze litery
	int i;
	int count=0;
	int adjacent_tiles_no = 0;
	//czy dane wlasciwe
	if(x<0 || x>=B_SIZE || y<0 || y>=B_SIZE) {
		fprintf(stderr, "Incorrect value of X or Y! X and Y need to be between 0 and %i!\n", TILE_START-1);
		return 0;
	} 
	for(i=0; i<TILE_START; i++) {
		if(tiles[i]==letter && letter!=0) count++;
	}
	if(count==0) {
		fprintf(stdout, "Invalid letter! You need to use letter from the pool of your tiles!\n");
		return 0;
	}
	//czy miejsce nie zajete
	if(board[x][y]!=0) {
		fprintf(stdout, "The position you want to place tile on, is already occupied! Select other place.\n");
		return 0;
	}
	//czy miejsce obok innych teils
	if(y!=0) {
		if(board[x][y-1]!=0) adjacent_tiles_no++;
	} 
	if(y!=B_SIZE-1) {
		if(board[x][y+1]!=0) adjacent_tiles_no++;
	} 
	if(x!=0) {
		if(board[x-1][y]!=0) adjacent_tiles_no++;
	} 
	if(x!=B_SIZE-1) {
		if(board[x+1][y]!=0) adjacent_tiles_no++;
	} 
	//fprintf(stderr, "adject tiles:%i  move_no:%i", adjacent_tiles_no, move_no);
	if (adjacent_tiles_no==0 && move_no!=0) {
		fprintf(stdout, "Tile must be placed near to other tile\n");
		return 0;
	} else {
		return 1;
	}
}

int get_starting_tiles(int fd, char tiles[TILE_START]) {
	int i=0;
	for(; i<TILE_START; i++) {
		tiles[i] = receive_letter(fd);
		if(check_if_opponent_connected(tiles[i]) == 0) return -1;
	}
	return 1;
}

int check_if_end(char tiles[TILE_START]) {
	int i;
	int count = 0;
	for(i=0; i<TILE_START; i++) {
		if(tiles[i]==0) count++;
	}
	if(count==TILE_START) {return 1;}
	else {return 0;}
}

int count_score(char board[B_SIZE][B_SIZE], int x, int y) {
	int score = 0;
	int tmp_score = 0;
	int ox = x;
	int oy = y;
	while(ox<B_SIZE && board[ox][oy]!=0) {
		tmp_score++;
		ox++;
	}
	ox = x-1;
	while(ox>=0 && board[ox][oy]!=0) {
		tmp_score++;
		ox--;
	}
	score = tmp_score;
	tmp_score = 0;
	ox = x;
	while(oy<B_SIZE && board[ox][oy]!=0) {
		tmp_score++;
		oy++;
	}
	oy = y-1;
	while(oy>=0 && board[ox][oy]!=0) {
		tmp_score++;
		oy--;
	}
	if (score<tmp_score)
		score = tmp_score;
	return score;
}

void display_score(int score, int opponent_score) {
	if (score>opponent_score) {
		fprintf(stdout, "You have won!\n");
		fprintf(stdout, "YOU:%i  to   OPPONENT:%i\n", score, opponent_score);
	}
	else if(score<opponent_score) {
		fprintf(stdout, "You lost :(\n");
		fprintf(stdout, "YOU:%i  to   OPPONENT:%i\n", score, opponent_score);
	} else {
		fprintf(stdout, "Draw!\n");
		fprintf(stdout, "YOU:%i  to   OPPONENT:%i\n", score, opponent_score);
	}
}

int repeat_game() {
	char repeat;
	while(1) {
		fprintf(stdout, "Game ended, do you want to play again? Y/n \n");
		scanf(" %c", &repeat);
		if(repeat == 'n' || repeat == 'N') return 0;
		else if (repeat == 'y' || repeat == 'Y') return 1;
		fprintf(stdout, "Invalid character\n");
	}
}

int get_player_number(int fd, int* is_first, int* move_no) {
	int32_t is_first_get;
	if(bulk_read(fd,(char *)&is_first_get,sizeof(int32_t))<(int)sizeof(int32_t)) ERR("read");
	*is_first = ntohl(is_first_get);
	if(*is_first == 2) {
		*move_no=1;
		fprintf(stdout, "PLAYER 2\n");
		fprintf(stdout, "Waiting on opponent's move...\n");
	} else if(*is_first == 1){
		fprintf(stdout, "PLAYER 1\n");
	}
	return check_if_opponent_connected(ntohl(is_first_get));
}

void send_score(int fd, int score) {
	int32_t s = htonl(score);
	if(bulk_write(fd,(char *)&s,sizeof(int32_t))<0) ERR("write:");
}

int receive_opponent_score(int fd, int* opponent_score) {
	int32_t s;
	if(bulk_read(fd,(char *)&s,sizeof(int32_t))<(int)sizeof(int32_t)) ERR("read");
	*opponent_score = ntohl(s);
	return check_if_opponent_connected(ntohl(s));
}
//server close connection
//get epipe from server (or sth)
//if want to play again reconnect


int doClient(int fd) {
	int i;
	int x, y;
	int is_move_valid = 0;
	char letter;
	char next_letter;
	char tiles[TILE_START];
	char board[B_SIZE][B_SIZE];
	memset(board,0,sizeof(board));
	//memset(tiles,1,sizeof(tiles));
	int is_first;
	int move_no = 0;
	int score = 0;
	int opponent_score;// = 0;
	
	//wait for start-up tiles
	fprintf(stdout, "Waiting for an opponent... \n");
	if(get_starting_tiles(fd, tiles)==0) return 0; //R
	fprintf(stdout, "Opponent found!\n");
	if(get_player_number(fd, &is_first, &move_no)==0) return 0; //R
	
	while(!check_if_end(tiles)) {
		if(receive_move(fd, &x, &y, &letter)==0) return 0; //R
		if(letter!=0) { 
			fprintf(stderr, "Opponent's move: x=%i y=%i letter=%c\n", x, y, letter);
			board[x][y]=letter;
		}
		print_board(board);
		print_tiles(tiles);
		fprintf(stdout, "Your turn! Make move!\n");
		is_move_valid = 0;
		while(!is_move_valid) {  //TODO: bug przy nieprawidlowych liczbach czasem
			fprintf(stdout, "X=");
			scanf("%i", &x);
			fprintf(stdout, "Y=");
			scanf("%i", &y);
			fprintf(stdout, "letter=");
			scanf(" %c", &letter);
			is_move_valid=move_validation(board, tiles, x, y, letter, move_no);
			move_no++;
		}
		fprintf(stderr, "Your move: x=%i y=%i letter=%c\n", x, y, letter);
		board[x][y]=letter;
		score = score+count_score(board, x, y);
		fprintf(stdout, "Your actuall score: %i\n", score);
		
		//get next letter
		next_letter=receive_letter(fd); //R
		if((int)next_letter == -1) return 0;
		if (next_letter==0) {
			fprintf(stdout, "No more avaible letters.\n");
		} else {
			fprintf(stdout, "New letter: %c\n", next_letter);
		}
		for(i=0; i<TILE_START; i++) {
			if(tiles[i] == letter)
				tiles[i]=next_letter;
		}
		
		send_move(fd, x, y, letter); //S
		fprintf(stdout, "Waiting on opponent's move...\n");	
	}
	fprintf(stdout, "No more tiles! End of game!\n");
	send_score(fd, score); //S
	receive_opponent_score(fd, &opponent_score); //R
	display_score(score, opponent_score);
	return 1;
}

int main(int argc, char** argv) {
	int fd;
	int is_game_repeated=1;
	int is_game_finished = 0;
	
	if(argc!=3) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if(sethandler(SIG_IGN,SIGPIPE)) ERR("Seting SIGPIPE:"); 
	//if(sethandler(sigin_handler, SIGINT)) ERR("Seting SIGINT:"); 
	fprintf(stderr,  "[%i] Cient is running!\n", getpid());
	while(is_game_repeated) {
		/*CONNECTION*/
		fd=connect_socket(argv[1],atoi(argv[2]));
		/*CLIENT WORK*/
		fprintf(stdout, "Connected to the server!\n");
		is_game_finished = doClient(fd);
		is_game_repeated = repeat_game();
		/*
	 	* Broken PIPE is treated as critical error here
	 	*/
		if(TEMP_FAILURE_RETRY(close(fd))<0)ERR("close");
	}
	return EXIT_SUCCESS;
}
