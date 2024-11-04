#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>


#define WINDOW_HEIGHT 480
#define WINDOW_WIDTH 480  
#define GRID_SIZE 8
#define SHIP_COUNT 5 // Number of unique ships (1 Battleship, 2 Cruisers, 2 Destroyers)
#define CELL_SIZE (WINDOW_WIDTH / GRID_SIZE)

// Struct to hold game state in shared memory
typedef struct {
    char parent_maze[GRID_SIZE * GRID_SIZE];
    char child_maze[GRID_SIZE * GRID_SIZE];
    int parent_remaining_ships;
    int child_remaining_ships;
    bool parent_turn;
} GameData;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *battleshipTexture;
SDL_Texture *destroyerTexture;
SDL_Texture *cruiserTexture;


// Function prototypes
void generate_maze(char* maze);
void print_maze(char* maze);
void shoot(char* target_maze, int* remaining_ships,GameData* gameData);
void sink_ship(char* target_maze, int row, int col, char ship_type);
void parent_turn(GameData* game_data, int* pipe_fd);
void child_turn(GameData* game_data, int* pipe_fd);
void drawBoard(SDL_Renderer* renderer,SDL_Texture* battleShipTexture,SDL_Texture* destroyerTexture,SDL_Texture* cruiserTexture,GameData* gameData);
int winningCondition(GameData* game_data);

int main(int argc, char *argv[]) {
    
     if (SDL_Init(SDL_INIT_VIDEO) < 0 || IMG_Init(IMG_INIT_PNG)<0) {
        printf("SDL or TTF initialization error: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("BATTLESHIP", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    battleshipTexture = IMG_LoadTexture(renderer,"battleship.png");
    destroyerTexture = IMG_LoadTexture(renderer,"destroyer.png");
    cruiserTexture = IMG_LoadTexture(renderer,"cruiser.png");

    if (!battleshipTexture || !cruiserTexture || !destroyerTexture ) {
    printf("Failed to load textures: %s\n", IMG_GetError());
    return 1; // Exit if textures could not be loaded
}

    srand(time(NULL));

    // Create shared memory for game data
    GameData* game_data = mmap(NULL, sizeof(GameData), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (game_data == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    // Initialize game data
    generate_maze(game_data->parent_maze);
    generate_maze(game_data->child_maze);
    game_data->parent_remaining_ships = SHIP_COUNT;
    game_data->child_remaining_ships = SHIP_COUNT;
    game_data->parent_turn = true;  // Parent goes first

    // Display initial grids
    printf("Parent's initial grid:\n");
    print_maze(game_data->parent_maze);
    printf("Child's initial grid:\n");
    print_maze(game_data->child_maze);

    // Create pipe for signaling
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("pipe failed");
        exit(1);
    }
    
    SDL_Event event;
    bool running = true;
    
    /*
    while (game_data->parent_remaining_ships > 0 && game_data->child_remaining_ships > 0 && running) {
        while (SDL_PollEvent(&event)){
            if(event.type == SDL_QUIT){
                running = false;
            }
            else{
                if (game_data->parent_turn) {
                    drawBoard(renderer,battleshipTexture,destroyerTexture,cruiserTexture,game_data);
                    parent_turn(game_data, pipe_fd);
                    drawBoard(renderer,battleshipTexture,destroyerTexture,cruiserTexture,game_data);

                } 
                else {
                    pid_t pid = fork();
                    if (pid == 0) {  // Child process
                        child_turn(game_data, pipe_fd);
                        exit(0);  // Child exits after its turn
                    } else if (pid > 0) {  // Parent process
                        wait(NULL);  // Wait for the child to finish
                        game_data->parent_turn = true;  // Parent's turn after child finishes
                    } else {
                        perror("fork failed");
                        exit(1);
                    }
                }
            }
          
        }
    }*/
    int turn = 0;
    int condition = 0;
    while (running) {
        // Polling SDL events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // Game logic for turn handling
        if (game_data->parent_turn) {
            parent_turn(game_data, pipe_fd);
        } else {
            pid_t pid = fork();
            if (pid == 0) {  // Child process
                child_turn(game_data, pipe_fd);
                exit(0);
            } else if (pid > 0) {
                waitpid(pid, NULL, 0); // Non-blocking wait
                game_data->parent_turn = true;
            }
        }

        // Drawing the board
        drawBoard(renderer, battleshipTexture, destroyerTexture, cruiserTexture, game_data);
        
        // Delay to allow SDL to process events
        SDL_Delay(16);  // Approximately 60 FPS

        turn++;
        condition = winningCondition(game_data);
        if (turn>2){
            if (condition != 0){ // i might need to update this part
            running = false;
            } 
        }
    }


    // Declare winner
    if (game_data->parent_remaining_ships == 0) {
        printf("Child wins!\n");
    } else {
        printf("Parent wins!\n");
    }

    // Clean up
    munmap(game_data, sizeof(GameData));
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    SDL_DestroyTexture(battleshipTexture);
    SDL_DestroyTexture(destroyerTexture);
    SDL_DestroyTexture(cruiserTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit(); // Quit the image subsystem
    SDL_Quit(); // Quit SDL
    return 0;
}

// Function to check if placement is valid (includes gap checks)
bool is_valid_placement(char* maze, int index, int length, bool horizontal) {
    int row = index / GRID_SIZE;
    int col = index % GRID_SIZE;

    for (int i = 0; i < length; i++) {
        int r = row + (horizontal ? 0 : i);
        int c = col + (horizontal ? i : 0);

        if (r >= GRID_SIZE || c >= GRID_SIZE || maze[r * GRID_SIZE + c] != 'O') return false;

        // Check surrounding cells for the gap rule
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                int adj_r = r + dr, adj_c = c + dc;
                if (adj_r >= 0 && adj_r < GRID_SIZE && adj_c >= 0 && adj_c < GRID_SIZE) {
                    if (maze[adj_r * GRID_SIZE + adj_c] != 'O') return false;
                }
            }
        }
    }
    return true;
}

// Place ships on the grid ensuring they have space between them
void place_ship(char* maze, char ship_type, int length) {
    bool placed = false;
    while (!placed) {
        int index = rand() % (GRID_SIZE * GRID_SIZE);
        bool horizontal = rand() % 2;  // Random orientation

        if (is_valid_placement(maze, index, length, horizontal)) {
            for (int i = 0; i < length; i++) {
                int r = (index / GRID_SIZE) + (horizontal ? 0 : i);
                int c = (index % GRID_SIZE) + (horizontal ? i : 0);
                maze[r * GRID_SIZE + c] = ship_type;
            }
            placed = true;
        }
    }
}

// Generate the maze with varied ships and constraints
void generate_maze(char* maze) {
    int map_size = GRID_SIZE * GRID_SIZE;
    for (int i = 0; i < map_size; i++) {
        maze[i] = 'O';
    }

    // Place each type of ship
    place_ship(maze, 'B', 4);  // 1 Battleship
    place_ship(maze, 'C', 3);  // 1st Cruiser
    place_ship(maze, 'C', 3);  // 2nd Cruiser
    place_ship(maze, 'D', 2);  // 1st Destroyer
    place_ship(maze, 'D', 2);  // 2nd Destroyer
}

// Prints the grid
void print_maze(char* maze) {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            printf("%c ", maze[i * GRID_SIZE + j]);
        }
        printf("\n");
    }
}

// Simulates a shot on the opponent's grid and sinks the entire ship if a segment is hit
void shoot(char* target_maze, int* remaining_ships,GameData* gameData) {
    int random_index = rand() % (GRID_SIZE * GRID_SIZE);
    int row = random_index / GRID_SIZE;
    int column = random_index % GRID_SIZE;

    char cell = target_maze[random_index];
    if (cell == 'B' || cell == 'C' || cell == 'D') {
        printf("Hit! %c-type ship at (%d, %d) starting to sink.\n", cell, row, column);

        // Call sink_ship to recursively mark the entire ship as sunk
        sink_ship(target_maze, row, column, cell);

        (*remaining_ships)--;
    } else {
        printf("Missed at (%d, %d).\n", row, column);
    }

    // Print the updated grid to visualize the game state after the shot
    printf("Target Maze After Shooting:\n");
    print_maze(target_maze);
    //drawBoard(renderer,battleshipTexture,destroyerTexture,cruiserTexture,gameData);
}

// Helper function to recursively mark all parts of a ship as sunk
void sink_ship(char* target_maze, int row, int col, char ship_type) {
    // Check if row and col are within bounds
    if (row < 0 || row >= GRID_SIZE || col < 0 || col >= GRID_SIZE) return;

    // Calculate the index for the 1D array
    int index = row * GRID_SIZE + col;

    // Check if the current cell is part of the ship to be sunk
    if (target_maze[index] != ship_type) return;

    // Mark the current cell as sunk
    target_maze[index] = 'X';

    // Recursively sink all segments of the ship
    sink_ship(target_maze, row - 1, col, ship_type);  // Up
    sink_ship(target_maze, row + 1, col, ship_type);  // Down
    sink_ship(target_maze, row, col - 1, ship_type);  // Left
    sink_ship(target_maze, row, col + 1, ship_type);  // Right
}

// Handles the parent's turn
void parent_turn(GameData* game_data, int* pipe_fd) {
    printf("\nParent's turn:\n");
    shoot(game_data->child_maze, &game_data->child_remaining_ships,game_data);

    // Delay for 1 second
    sleep(1);

    // Check if child has any remaining ships
    if (game_data->child_remaining_ships > 0) {
        game_data->parent_turn = false;  // Child's turn next
        write(pipe_fd[1], "go", 2);  // Signal child to play
    }
    //drawBoard(renderer,battleshipTexture,destroyerTexture,cruiserTexture,game_data);
}

// Handles the child's turn
void child_turn(GameData* game_data, int* pipe_fd) {
    char buffer[2];
    read(pipe_fd[0], buffer, 2);  // Wait for signal from parent
    printf("\nChild's turn:\n");
    shoot(game_data->parent_maze, &game_data->parent_remaining_ships,game_data);

    // Delay for 1 second
    sleep(1);
}

int winningCondition(GameData* game_data){ // 0 if the game continues, 1 if parent wins, 2 if child wins
    int winningCondition = 0;

    if (game_data->parent_remaining_ships > 0 && game_data->child_remaining_ships == 0)
        winningCondition = 1;
    else if (game_data->child_remaining_ships > 0 && game_data->parent_remaining_ships == 0)
        winningCondition = 2;    

    return winningCondition;    
}

void drawBoard(SDL_Renderer* renderer,SDL_Texture* battleShipTexture,SDL_Texture* destroyerTexture,SDL_Texture* cruiserTexture,GameData* gameData){
    
    char* board;

    if (gameData->parent_turn)
        board = gameData->child_maze; // alias // not really sure about this
    else
        board = gameData->parent_maze;


    // Clear the screen
    SDL_SetRenderDrawColor(renderer, 34, 0, 255, 255);
    SDL_RenderClear(renderer);

      // Draw grid
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    for (int i = 1; i < GRID_SIZE; i++) {
        SDL_RenderDrawLine(renderer, i * CELL_SIZE, 0, i * CELL_SIZE, WINDOW_HEIGHT);
        SDL_RenderDrawLine(renderer, 0, i * CELL_SIZE, WINDOW_WIDTH, i * CELL_SIZE);
    }

     // Draw ships using textures
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (board[i*GRID_SIZE+j] == 'B') {
                SDL_Rect dstRect = {j * CELL_SIZE, i * CELL_SIZE, CELL_SIZE, CELL_SIZE};
                SDL_RenderCopy(renderer, battleShipTexture, NULL, &dstRect); // Draw battleship
            } else if (board[i*GRID_SIZE+j] == 'C') {
                SDL_Rect dstRect = {j * CELL_SIZE, i * CELL_SIZE, CELL_SIZE, CELL_SIZE};
                SDL_RenderCopy(renderer, cruiserTexture, NULL, &dstRect); // Draw cruiser
            }
             else if (board[i*GRID_SIZE+j] == 'D') {
                SDL_Rect dstRect = {j * CELL_SIZE, i * CELL_SIZE, CELL_SIZE, CELL_SIZE};
                SDL_RenderCopy(renderer, destroyerTexture, NULL, &dstRect); // Draw destroyer
            }
        }    
        
    }

    // Show the updated screen
    SDL_RenderPresent(renderer);
}