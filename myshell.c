#define True 1
#define False 0
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>


void smaller_tokens(int, int, char*);
void sigchld_handler();


//Create tokens based on pipe, then error check

void make_tokens(char* command){
        char pipe_symbol = '|';
        int i = 0;
        int j = 0;
        char line[512];

        char tokens[32][512] = {{0}};
        int k =0;

        while(command[i] != '\0'){
                if(command[i] == ' ' && command[i+1] == ' '){
                        i++;
                        continue;
                }

                if(command[i] == ' ' && command[i-1] == pipe_symbol){
                        i++;
                        continue;
                }

                if((command[i] == ' ') && (command[i+1] == pipe_symbol || command[i+1] == '\0')){   //get rid of whitespace at end of tokens
                        i++;
                        continue;
                }

                if((command[i] == '<' || command[i] == '>') && command[i-1] != ' '){
                        line[j] = ' ';
                        j++;
                }
                if(command[i] != ' ' && (command[i-1] == '<' || command[i-1] == '>')){
                        line[j] = ' ';
                        j++;
                }
                if(command[i] == '&' && command[i-1] != ' '){
                        line[j] = ' ';
                        j++;
                }

                if(command[i] == pipe_symbol){
                        strcpy(tokens[k], line);
                        k++;
                        line[0] = '\0';
                        j = 0;
                }
                else{
                        if(j == 0 && command[i] == ' '){
                                i++;
                                continue;
                        }
                        line[j] = command[i];
                        line[j+1] = '\0';
                        j++;
                }

                i++;
                if (command[i] == '\0' || command[i] == '\n'){
                        strcpy(tokens[k], line);
                        k++;
                        break;
                }
         }

        //ERROR CHECKING
        int ii;
        int x;
        for(ii = 0; ii<k; ii++){
                x = 0;
                char prev;
                while(tokens[ii][x] != '\0'){
                        if(tokens[ii][x] == ' '){
                                x++;
                                continue;
                        }
                        if(tokens[ii][x] == '<' || tokens[ii][x] == '>'){
                                if(prev == '<' || prev == '>'){
                                        printf("ERROR: only one direction for each command\n");
                                        return;
                                }
                                if(k>0 && tokens[ii][x] == '<' && ii != 0){
                                        printf("ERROR: input redirection not in first command\n");
                                        return;
                                }
                                if(k>0 && tokens[ii][x] == '>' && ii != k-1){
                                        printf("ERROR: output redirection not in last command\n");
                                        return;
                                }
                        }
                        if(tokens[ii][x] == '&' && (ii != k-1 || tokens[ii][x+1] != '\0')){
                                printf("ERROR: & not at end of line\n");
                                return;
                        }
                        prev = tokens[ii][x];
                        x++;
                }
        }

        //CREATE PIPE

        int pipe_in = 0;
        int fd[2];
        for(ii=0; ii<k-1; ii++){
                pipe(fd);
                smaller_tokens(pipe_in, fd[1], tokens[ii]);
                close(fd[1]);
                pipe_in = fd[0];
        }

        if(pipe_in != 0){
                dup2(pipe_in,0);
        }

        smaller_tokens(pipe_in, 1, tokens[k-1]);

        //ENSURE STDIN IS OPEN
        freopen("/dev/tty","r",stdin);


}


//Tokenize further based on spaces, then fork exec
void smaller_tokens(int in, int out, char* token){
        char* smaller;
        char* exec_args[32];
        int token_count = 0;
        int redirect_out = False;
        int redirect_in = False;
        char* output_file;
        char* input_file;
        int background = 0;

        //DETECT BACKGROUND PROCESS
        if(strstr(token,"&")){
                background = 1;
        }

        //CREATE EXEC ARGS
        smaller = strtok(token," ");
        while(smaller != NULL && token_count < 32){

                if(strcmp(smaller,"&") == 0){
                        break;
                }

                if(strcmp(smaller,">") == 0){
                        redirect_out = True;
                        smaller = strtok(NULL," ");
                        if(smaller != NULL)
                                output_file = smaller;
                        smaller = strtok(NULL," ");
                        if(smaller == NULL)
                                break;

                }
                else if(strcmp(smaller,"<") == 0){
                        redirect_in = True;
                        smaller = strtok(NULL," ");
                        if(smaller != NULL)
                                input_file = smaller;
                        smaller = strtok(NULL," ");
                        if(smaller == NULL)
                                break;
                }
                else{
                        exec_args[token_count] = smaller;
                        token_count++;
                        smaller = strtok(NULL," ");
                }

        }

        exec_args[token_count] = NULL;

        int status;

        //SET SIGNAL HANDLER
        signal(SIGCHLD, sigchld_handler);

        //FORK AND EXEC
        pid_t pid = fork();
        if(pid != 0){
        //PARENT PROCESS

                //DONT WAIT IF BACKGROUND
                if(!background)
                        waitpid(pid,&status,0);

        }
        else{
        //CHILD PROCESS

                //Handle redirection fd
                if(redirect_out || redirect_in){

                        if(redirect_out){
                                int fd = open(output_file,O_WRONLY | O_CREAT | O_TRUNC, 0744);
                                if(fd < 0){
                                        perror("open");
                                        exit(1);
                                }

                                close(1);  //close std out
                                dup2(fd,1);
                                close(fd);
                        }
                        if(redirect_in){
                                int fd = open(input_file,O_RDONLY);
                                if(fd < 0){
                                        perror("open");
                                        exit(1);
                                }


                                close(0);
                                dup2(fd,0);
                                close(fd);
                        }
                }

                //Handle piping fds
                if(in != 0){
                        dup2(in,0);
                        close(in);
                }

                if(out != 1){
                        dup2(out,1);
                        close(out);
                }

                execvp(exec_args[0],exec_args);
                perror("execvp failed");
                exit(1);
        }

}


//SIGCHLD HANDLER
void sigchld_handler(){
        int status;

        //Continuously check from parent if child terminated
        while(waitpid(-1, &status, WNOHANG) > 0){
                if(WIFEXITED(status))
                        //child exited normally
                        continue;
        }

}



//While loop prompts for input, then calls function to parse
int main(int argc, char* argv[]){
        char command[512];
        int dont_prompt = 0;

        if(argc>1 && (strcmp(argv[1],"-n") == 0)){
                dont_prompt = 1;
        }

        while(True){
                if(!dont_prompt){
                        printf("my_shell$ ");
                        fflush(stdout);
                }
                if(fgets(command,sizeof(command),stdin) == NULL){    //handles CTRL^d
                        //perror("fgets");
                        break;
                }
                if(command[0] == '\n'){
                        continue;
                }
                make_tokens(command);
        }

        return 0;
}
