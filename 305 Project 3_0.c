#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>


#define GRID_SIZE 8
#define SHIP_COUNT 5  // Number of unique ships (1 Battleship, 2 Cruisers, 2 Destroyers)

// Struct to hold game state in shared memory
typedef struct {
    char parent_maze[GRID_SIZE * GRID_SIZE];
    char child_maze[GRID_SIZE * GRID_SIZE];
    int parent_remaining_ships;
    int child_remaining_ships;
    bool parent_turn;
} GameData;

// Save game state structure
typedef struct {
    GameData game_state;
    time_t save_time;
} SaveGame;

// Global pointer for signal handler
GameData* game_data_global = NULL;

// Function prototypes
void generate_maze(char* maze);
void print_maze(char* maze);
void shoot(char* target_maze, int* remaining_ships);
void sink_ship(char* target_maze, int row, int col, char ship_type);
void parent_turn(GameData* game_data, int* pipe_fd);
void child_turn(GameData* game_data, int* pipe_fd);
void save_game_state(GameData* game_data);
bool load_game_state(GameData* game_data);
void setup_autosave(GameData* game_data);
void handle_interrupt(int signum);
bool is_valid_placement(char* maze, int index, int length, bool horizontal);
void place_ship(char* maze, char ship_type, int length);

// Function to save game state
void save_game_state(GameData* game_data) {
    SaveGame save;
    save.game_state = *game_data;
    save.save_time = time(NULL);
    
    FILE* save_file = fopen("battleship_save.dat", "wb");
    if (save_file == NULL) {
        perror("Error opening save file");
        return;
    }
    
    fwrite(&save, sizeof(SaveGame), 1, save_file);
    fclose(save_file);
    printf("Game state saved successfully!\n");
}

// Function to load game state
bool load_game_state(GameData* game_data) {
    SaveGame save;
    
    FILE* save_file = fopen("battleship_save.dat", "rb");
    if (save_file == NULL) {
        printf("No saved game found.\n");
        return false;
    }
    
    if (fread(&save, sizeof(SaveGame), 1, save_file) != 1) {
        printf("Error reading save file.\n");
        fclose(save_file);
        return false;
    }
    
    fclose(save_file);
    
    // Check if save is older than 24 hours
    time_t current_time = time(NULL);
    if (difftime(current_time, save.save_time) > 24 * 60 * 60) {
        printf("Save file is too old (>24 hours). Starting new game.\n");
        return false;
    }
    
    *game_data = save.game_state;
    printf("Game state loaded successfully! (Saved %s)", ctime(&save.save_time));
    return true;
}

// Function to handle auto-save timer
void setup_autosave(GameData* game_data) {
    static int move_counter = 0;
    move_counter++;
    
    if (move_counter % 5 == 0) {
        save_game_state(game_data);
    }
}

// Signal handler for interrupts
void handle_interrupt(int signum) {
    printf("\nGame interrupted. Saving state...\n");
    save_game_state(game_data_global);
    exit(0);
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
    printf("  ");
    for (int i = 0; i < GRID_SIZE; i++) {
        printf("%d ", i);
    }
    printf("\n");

    for (int i = 0; i < GRID_SIZE; i++) {
        printf("%d ", i);
        for (int j = 0; j < GRID_SIZE; j++) {
            printf("%c ", maze[i * GRID_SIZE + j]);
        }
        printf("\n");
    }
}

// Simulates a shot on the opponent's grid and sinks the entire ship if a segment is hit
void shoot(char* target_maze, int* remaining_ships) {
    int random_index = rand() % (GRID_SIZE * GRID_SIZE);
    int row = random_index / GRID_SIZE;
    int column = random_index % GRID_SIZE;

    char cell = target_maze[random_index];
    if (cell == 'B' || cell == 'C' || cell == 'D') {
        printf("Hit! %c-type ship at (%d, %d) starting to sink.\n", cell, row, column);
        sink_ship(target_maze, row, column, cell);
        (*remaining_ships)--;
    } else {
        printf("Missed at (%d, %d).\n", row, column);
    }

    printf("Target Maze After Shooting:\n");
    print_maze(target_maze);
}

// Helper function to recursively mark all parts of a ship as sunk
void sink_ship(char* target_maze, int row, int col, char ship_type) {
    if (row < 0 || row >= GRID_SIZE || col < 0 || col >= GRID_SIZE) return;

    int index = row * GRID_SIZE + col;
    if (target_maze[index] != ship_type) return;

    target_maze[index] = 'X';

    sink_ship(target_maze, row - 1, col, ship_type);  // Up
    sink_ship(target_maze, row + 1, col, ship_type);  // Down
    sink_ship(target_maze, row, col - 1, ship_type);  // Left
    sink_ship(target_maze, row, col + 1, ship_type);  // Right
}

// Handles the parent's turn
void parent_turn(GameData* game_data, int* pipe_fd) {
    printf("\nParent's turn:\n");
    shoot(game_data->child_maze, &game_data->child_remaining_ships);
    setup_autosave(game_data);

    sleep(1);

    if (game_data->child_remaining_ships > 0) {
        game_data->parent_turn = false;
        write(pipe_fd[1], "go", 2);
    }
}

// Handles the child's turn
void child_turn(GameData* game_data, int* pipe_fd) {
    char buffer[2];
    read(pipe_fd[0], buffer, 2);
    printf("\nChild's turn:\n");
    shoot(game_data->parent_maze, &game_data->parent_remaining_ships);
    setup_autosave(game_data);

    sleep(1);
}


int main() {
    srand(time(NULL));

    // Create shared memory for game data
    GameData* game_data = mmap(NULL, sizeof(GameData), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (game_data == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }
      // Set up signal handler
    game_data_global = game_data;
    signal(SIGINT, handle_interrupt);

      // Try to load saved game
    if (!load_game_state(game_data)) {
        // Initialize new game if no save exists
        generate_maze(game_data->parent_maze);
        generate_maze(game_data->child_maze);
        game_data->parent_remaining_ships = SHIP_COUNT;
        game_data->child_remaining_ships = SHIP_COUNT;
        game_data->parent_turn = true;
    }

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

    while (game_data->parent_remaining_ships > 0 && game_data->child_remaining_ships > 0) {
        if (game_data->parent_turn) {
            parent_turn(game_data, pipe_fd);
        } else {
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
    return 0;
}
