#include "utility.hpp"

pid_t childPid; // pid of most recent child process
vector<string> cmds; // vector to store commands
// background flag to check if a child process is running in background
// scaninterrupt flag to check if a signal has been received
int scaninterrupt = 0, background = 0; 


// function to handle SIGINT (Ctrl+C)
// signum = SIGINT = 2 (in most systems)
void sigint_handler(int signum) // signum is the signal number
{
    // Stops execution of the process running in the foreground only
    // There exists a child process running in foreground
    if((!background) && (childPid >= 0)){
        // getpgid() returns the process group ID of the child process 
        // killpg() sends the signal SIGINT to the child's entire process group
        // this ensures that the child and also its children are killed
        killpg(getpgid(childPid), SIGINT); // send SIGINT signal to the process group of the child process
        cout << endl; // print a newline character
        return;
    }
    // No child or child running in background
    scaninterrupt = 1; // indicates that the user interrupted the shell
    cout << endl; // Provides visual feedback that the Ctrl+C action was successful
    rl_forced_update_display(); // refreshes the display prompt so that the user can see the prompt
}

// function to handle SIGTSTP (Ctrl+Z)
void sigtstp_handler(int signum)
{
    // No currently running command (Since no children)
    if(childPid == -1){

        cout << endl;
        // No running process to send into background
        cout << "No process to send in background" ;
        cout << endl;
        // Print current working directory and prompt again
        cout << getcwd((char*)NULL, size_t(0)) << PROMPT;
        return;
    }
    else
        // getpid() returns the process ID of the shell process
        // kill sends the signal SIGCHLD to the shell process
        kill(getpid(), SIGCHLD);
}

// function to handle SIGCHLD
// SIGCHLD is sent to the parent process when a child process terminates to notify it that one of its children has terminated
void sigchld_handler(int signum)
{
    if(!background) return; // if no child process is running in background, return
    scaninterrupt = 1; // indicates that the user interrupted the shell
    cout.flush();
}

void run()
{
    // s is the command entered by the user
    // inputfile is the file from which input is to be read
    // outputfile is the file to which output is to be written
    // prompt is the prompt displayed to the user
    string s, inputfile, outputfile, prompt;
    // input is the character array to store the command entered by the user
    char input[1000];
    cout.flush();

    prompt = getcwd((char*)NULL, size_t(0));
    prompt += PROMPT; // prompt for shell which is displayed to the user

    cin.clear();
    // display the prompt to the user and read the command entered by the user
    s = readline(prompt.c_str());

    // if no command is entered, return; run is called again
    if(stringEmpty(s)) 
        return;
    
    // add command to history
    add_history((char*)s.c_str());

    // implementing the exit command
    if(s == "exit") {
        // Write the previous SIZE commands to the history file (not append)
        write_history();
        // exit the shell
        exit(0);
    }

    cmds.push_back(s);
    vector<string> v; 
    // Split the command into tokens
    // Each token is either a special character, a quoted string (with quotes removed now), or a normal string  
    // The background flag is set if the command is to be run in the background
    // Note that split is a user defined function in utility.cpp
    vector<pair<string,int>> pair_vec = split(s, &background);
    // Each pair in pair_vec stores the token and a flag indicating whether the token contains a wildcard or not

    for (int i = 0; i < pair_vec.size(); i++)
    {
        if(pair_vec[i].second == 1){ // if the token contains a wildcard
            // wildcard_handler is a user defined function in utility.cpp
            // it returns a vector of strings containing the filenames that match the given pattern
            // appended to their paths
            vector<string> temp = wildcard_handler(pair_vec[i].first);
            if(temp.size() == 0) // if no filenames match the given pattern
            {
                v.push_back(pair_vec[i].first); // add the token to the vector as it is without matching
            }
            for(int j = 0; j<temp.size(); j++){ // for each filename that matches the given pattern
                v.push_back(temp[j]);
            }
        }
        else{ // if the token does not contain a wildcard
            v.push_back(pair_vec[i].first);
        }
    }

    int fd, pipe_fd[2], prev_pipe_fd[2];
    int parity = 0, initial = 0;
    int set = 0;
    FILE* fpin;

    if(v[0] == "cd") // change directory command
    {
        chdir(v[1].c_str()); // change the current working directory to the directory specified by the user
        return; 
    }

    if (v[0] == "delep"){ // delete without prejudice command

        char* filename = (char*)v[1].c_str(); // filename to be deleted
        delep(filename); // user defined function in utility.cpp
        return;
    }

    if (v[0] == "sb"){ // squash bug command

        int argc = v.size();
        char* argv[argc];

        for(int i = 0; i < argc; i++){
            argv[i] = (char*)v[i].c_str(); // convert each string (token) to a character array
        }

        sb(argc, argv); // user defined function in utility.cpp. Identifies malware in the system
        return;
    }

    childPid = fork(); // Execute other commands by creating a child process and replacing the child process image with the new process image
    // For pipe chains we create multiple child processes with proper IO redirections
    if(childPid == 0) // child process
    {
        // prev_cmd is the index of the previous command in the vector
        // flg_init is the index of the first token of the current command
        // flg_cnt is the number of tokens in the current command
        int prev_cmd = 0, flg_init = 0, flg_cnt = 0;
        for(int i = 0; i < v.size(); i++) // Iterate over the tokens in the command
        {
            // Format: < inputfile > outputfile command1 | command2 | command3
            if(v[i] == "<") // input redirection 
            {
                flg_cnt = i - flg_init; // number of tokens in the current command
                flg_init = i + 1; // index of the first token of the next command
                set = 1; // flag to indicate whether input redirection is set or not
                inputfile = v[i+1];

                if((fd = open((char*)inputfile.c_str(),  O_RDONLY)) < 0){ // open the user specified input file to replace standard input
                    perror("Can't open file");
                    exit(0);                    
                }

                dup2(fd,0); // duplicate the file descriptor fd to the standard input file descriptor 0
                close(fd);  
            }

            else if(v[i]=="|"){ // pipe

                if(set){
                    set = 0; 
                }
                else{
                    flg_cnt = i - flg_init;
                }

                flg_init = i + 1;
                pipe(pipe_fd); // set file descriptors for the pipe
                // pipe_fd[0] is the file descriptor for the read end of the pipe
                // pipe_fd[1] is the file descriptor for the write end of the pipe

                if(parity){ 
                    prev_pipe_fd[1] = pipe_fd[0]; // set the write end of the previous pipe as the read end of the current pipe
                    // this is done to ensure that the output of the previous command is the input of the current command
                }
                else
                {
                    prev_pipe_fd[0] = pipe_fd[0]; // set the read end of the previous pipe as the read end of the current pipe                    
                }

                // fork a child process
                if(fork() == 0) // child process representing the previous command
                {
                    // Pipes are uni-directional. If a process tries to read data when it hasn't been written, the process is blocked until data becomes available.
                    close(pipe_fd[0]); // child can't read from the pipe
                    dup2(pipe_fd[1], 1); // duplicate the write end of the pipe to the standard output file descriptor 1
                    // this ensures that the output written to the standard output by the child is written to the write end of the pipe
                    char* args[v.size() + 1];
                    int j;

                    for(j = prev_cmd; j < (prev_cmd+flg_cnt); j++) // execute the previous command and write the output to the pipe
                    {
                        if(v[j] == "<" || v[j]==">") break; // break if input or output redirection is encountered
                        args[j-prev_cmd] = (char*)v[j].c_str(); // convert the string to a character array
                    }
                    args[j-prev_cmd] = NULL; 
                    if(execvp(args[0], args) == -1) // replace the child process image with the new process image.
                    // if successful, the rest of the code will never run for this child  
                    {
                        cout << "Command not found" << endl;
                        exit(0);
                    }
                }

                wait(NULL); // wait for the child process to finish
                close(pipe_fd[1]); // close the write end of the pipe
                dup2(pipe_fd[0], 0); // duplicate the read end of the pipe to the standard input file descriptor 0

                if(initial) // whether the command is the first command in a pipeline
                {
                    if(parity) // odd or even numbered command in the pipeline
                    {
                        close(prev_pipe_fd[0]); 
                    }
                    else
                    {
                        close(prev_pipe_fd[1]);
                    }
                }
                else
                {
                    initial = 1;
                }
                parity = 1 - parity;
                prev_cmd = i+1;
            }

            else if(v[i]==">"){ // output redirection
                flg_cnt = i- flg_init;
                flg_init = i + 1;
                outputfile = v[i+1];
                fd = open((char*)outputfile.c_str() , O_RDWR | O_CREAT, 0666); // open the user specified output file to replace standard output
                dup2(fd, 1); // duplicate the file descriptor fd to the standard output file descriptor 1
                close(fd);
            }
        }
        // no pipe in the command
        char* args[v.size() + 1]; 
        int j;
        for(j = prev_cmd; j < v.size(); j++)
        {
            if((v[j] == "<") || (v[j] == ">") || (v[j]=="&")) break;
            args[j-prev_cmd] = (char*)v[j].c_str();
        }
        args[j-prev_cmd] = NULL;
        if(execvp(args[0], args) < 0) // replace the child process image with the new process image
        {
            cout << "Command not found" << endl;
            exit(0);
        }
    }

    // NULL specifies that the parent process is not interested in the exit status of the child process
    if(!background) waitpid(childPid, NULL, 0); // wait for the child process to finish if it is running in the foreground
    // WNOHANG specifies that the parent process should not wait for the child process to finish
    else waitpid(childPid, NULL, WNOHANG); 
}

int main()
{
    initialize_readline(); // Bind keys to functions, i.e. up arrow, down arrow, ctrl+a, ctrl+e 
    read_history(); // read old history from file

    /*
    Signals are a way for the operating system to communicate with running processes. 
    By default, most signals terminate the process.
    But we can override this behavior by specifying a custom signal handler.
    */
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    signal(SIGCHLD, sigchld_handler);

    // siginterrupt() is used to specify whether a system call should be interrupted by a signal handler
    // in the middle of execution or not.
    siginterrupt(SIGINT, 1); // 1 means that the system call should be interrupted
    siginterrupt(SIGTSTP, 1);
    
    while(1)
    {
        childPid = -1; // no child process running
        background = 0; // no child process running in background
        run(); // run the shell until the user exits
        // Each run() call reads a command from the user, processes it, and executes it
        // The program will only exit when the user types the "exit" command
    }
    return 0;
}