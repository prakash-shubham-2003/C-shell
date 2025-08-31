#include "utility.hpp"

// variables for history
// fphist is the file pointer to the history file
FILE* fphist;
// last_cmd is the last command entered but not yet executed
string last_cmd;

int counter = 0;

// command history structure
history_state cmd_history;
// name of history file
const char *history_file = ".cmd_history";

// variables for delep
const char *proc_path = "/proc/";
const char *lock_file = "/proc/locks";

// function to check if string is empty
bool stringEmpty(string s)
{
    for(char c: s)
    {
        if(c != ' ' && c != '\t' && c != '\n') return false;
    }
    return true;
}

// function to check if string is a number
bool is_number(string s)
{
    auto it = s.begin();
    for(;(it!=s.end()) && (isdigit(*it)); it++){
        // check if all characters in the string are digits
    }
    return !s.empty() && it == s.end();
}

// function to split the command into tokens
vector<pair<string,int>> split(string s1, int* background)
{
    // vector to store the resulting tokens
    vector<pair<string,int>> v;
    // a string to accumulate characters of the current token
    string s = "";
    // flag to indicate if the current token has a wildcard
    int wildcard = 0;
    // A pair to temporarily hold a token and its wildcard status before adding it to the vector
    pair<string,int> p;

    for(int i = 0; i < s1.size(); i++)
    {
        if(s1[i]==' ') continue; // space between tokens
        // |, <, >, & are special characters
        while(s1[i] != '|' && s1[i] != '<' && s1[i] != '>' && s1[i] != '&' && s1[i] != ' ' && (i<s1.size())) 
        {
            // check for strings enclosed in quotes (allowed to contain spaces)
            if(s1[i] == '\"'){
                i++;
                // find the end of the quoted string
                while(s1[i] != '\"' && (i<s1.size())){
                    s += s1[i]; // Push the characters between the quotes to the string
                    i++;
                }
                i++; // Move to the next character after the closing quote
                // If the next character is a space or a special character, add the string to the vector
                if(s1[i] == ' ' || s1[i] == '|' || s1[i] == '<' || s1[i] == '>' || s1[i] == '&'){
                    p = make_pair(s,wildcard);
                    v.push_back(p);
                    s="";
                    i--;
                    break;
                }
            }
            // check for strings enclosed in single quotes
            if(s1[i] == '\''){
                i++;
                while(s1[i] != '\'' && (i<s1.size())){
                    s += s1[i];
                    i++;
                }
                // same as above
                i++;
                if(s1[i] == ' ' || s1[i] == '|' || s1[i] == '<' || s1[i] == '>' || s1[i] == '&'){
                    p = make_pair(s,wildcard);
                    v.push_back(p);
                    s="";
                    i--;
                    break;
                }
            }
            // Check for wildcard characters or a \ which will indicate that the token consists of multiple sub-tokens
            // Eg. The token home\user\ contains the tokens home and user, so it has to be split further later 
            // Thus for now, we mark \ also as a wildcard because it has to be handled separately later
            if(((s1[i] == '*') || (s1[i] == '?')) && (s1[i-1] != '\\')){
                wildcard = 1;
            }
            // Add non-special characters to the current token (which is not a quoted string)
            s += s1[i];
            i++;
        }
        if(s!=""){ // Regular token (maybe with wildcard), but not a quoted string or special character
            p = make_pair(s,wildcard);
            v.push_back(p);
            wildcard = 0;
            s="";
        }
        // Handle special characters
        if((s1[i] == '|' || s1[i] == '<' || s1[i] == '>' || s1[i] == '&') && (i<s1.size())){
            if(s1[i] == '&'){ // Check for background process
                *background = 1;
            }
            s+=s1[i];
            p = make_pair(s,0);
            v.push_back(p); // Tokenize the special character
        }
        s="";
        wildcard = 0; // Reset wildcard flag
    }

    return v;
}

// function to split the tokens containing / into sub-tokens with wildcard flags (remove the / and create new tokens)
vector<pair<string,int>> wildcard_split(string s)
{
    int count = 0; // count number of '/' in the command
    for(int i=0; i<s.size(); i++){
        if(s[i] == '/'){
            count++;
        }
    }

    vector<pair<string,int>> v; // stores pairs of token and wildcard flag
    pair<string,int> p; // pair to store token and wildcard flag in order to push to vector v
    string str=""; // string to store the current token
    int wildcard = 0; // flag to indicate wildcard for each token
    p = make_pair(str,wildcard);

    for(int i=0; i<s.size(); i++){
        if(s[i]=='/'){ // if '/' is encountered, add the current token to the vector
            count--;
            p.first = str; 
            p.second = wildcard; 
            v.push_back(p);
            str="";
            wildcard = 0;
        }
        else{
            // check for wildcard characters
            if(s[i] == '*'){
                wildcard = 1;
            }
            else if(s[i] == '?'){
                wildcard = 1;
            }
            // add characters to the token (except /)
            str+=s[i];
        }
    }

    p.first = str;
    p.second = wildcard;
    v.push_back(p);
    return v;
}

// function to handle wildcard commands
vector<string> wildcard_handler(string s)
{
    // Splits the token further into smaller tokens based on the character '/'
    vector<pair<string,int>> v = wildcard_split(s); // pairs of token and wildcard flag
    // stores the filenames that match the given pattern
    vector<string> result;
    // string to accumulate the directory path
    string p = "";
    result.push_back(p);
    p="";
    DIR *dir;
    /*
    Explanation:
    If the token contains no slashes (i.e. v.size() == 1), then the token is matched with the filenames 
    in the current directory and that's it.
    If the token contains slashes (i.e. v.size() > 1), then the token is split into sub-tokens based on the slashes.
    The first sub-token is matched with the filenames in the current directory.
    The subsequent sub-tokens are matched with the filenames in the directories obtained by appending the results 
    from matching the previous sub-tokens.
    */
    for(int i=0; i<v.size(); i++){ // for each token
        int size = result.size();
        for(int j=0; j<size; j++) // for each directory path
        {
            if(v[i].second == 1) // if the token contains a wildcard
            {
                if(result[0]==""){
                    dir = opendir ("."); // opendir() opens the directory specified by the path
                }
                else{
                    dir = opendir (result[0].c_str()); // open the directory path
                }
                if(dir != NULL) { // if the directory exists
                    struct dirent *ent; // header file structure to store information about directory entries
                    while ((ent = readdir (dir)) != NULL) { // read the directory
                        // Internal function to match the token with the filename (case insensitive)
                        if(fnmatch(v[i].first.c_str(), ent->d_name, FNM_CASEFOLD) == 0){ 
                            p = ent->d_name; // store the filename
                            result.push_back(result[0] + p + "/"); // append the filename to the directory path
                        }
                    }
                    result.erase(result.begin()); // remove the older directory path and keep only the new path(s)
                    closedir (dir);
                } else {
                    perror ("");
                }
            }
            else{ // if the token does not contain a wildcard
                result.push_back(result[0] + v[i].first + "/"); // append the token to the directory path
                result.erase(result.begin()); // remove the older directory path and keep only the new path
            }
        }
    }
    for(int i=0; i<result.size(); i++){ // remove the last character '/' from the directory path
        result[i] = result[i].substr(0,result[i].size()-1);
    }
    return result; // return the filenames that match the given pattern
}

// read history from file
void read_history() {

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    fphist = fopen(history_file, "r");
 
    while ((read = getline(&line, &len, fphist)) != -1)
    {
        // added to remove newline character from getline input
        if (line[read - 1] == '\n')
            line[read - 1] = '\0';
        cmd_history.history.push_back(string(line)); // convert char* to string and push to deque
    }

    cmd_history.size = cmd_history.history.size();
    cmd_history.index = cmd_history.size - 1; // set index to last command in history

    fclose(fphist);
    if (line) // if line was allocated (not NULL) memory, free it
        free(line);
}

// write deque history to file
void write_history(){
    // Completely overwrite the history file with the current history of last SIZE commands
    fphist = fopen(history_file, "w");
    for (int i=0; i<cmd_history.size; i++){
        fputs(cmd_history.history[i].c_str(), fphist);
        putc('\n', fphist);
    }
    fclose(fphist);
}

// function to add terminal command to deque and file
void add_history(char* s)
{
    // add command to deque
    cmd_history.history.push_back(string(s));
    cmd_history.size++;

    // set last unexecuted command to empty and set index to this command
    cmd_history.index = cmd_history.size;
    last_cmd = "";

    // open file in append mode and write command to file
    fphist = fopen(history_file, "a");
    fputs(s, fphist);
    putc('\n', fphist);
    fclose(fphist);

    // remove first command if history size exceeds SIZE
    if (cmd_history.size > SIZE)
    {
        cmd_history.history.pop_front();
        cmd_history.size--;
        cmd_history.index--;
    }
}

/* 
    Functions :

    Removes any text on the current terminal line
    Displays the previous history line
    If the first line is reached, the cursor stays on the first line
*/
int backward_history(int count, int key)
{
    if(cmd_history.index<cmd_history.size){
        cmd_history.history[cmd_history.index] = string(rl_line_buffer);
    }

    counter--;
    if(counter==-1){
        last_cmd = string(rl_line_buffer);
    }
    if (cmd_history.index > 0)
        cmd_history.index--;
    
    if (cmd_history.index >= 0){

        string s;
        s = cmd_history.history[cmd_history.index];
        rl_replace_line(s.c_str(), 0);
        rl_redisplay();
        rl_point = rl_end;
    }
    return 0;
}

/* 
    Functions :

    Removes any text on the current terminal line
    Displays the next history line
    If the last line is reached, the cursor stays on the last line
*/
int forward_history(int count, int key)
{
    if(cmd_history.index<cmd_history.size){
        cmd_history.history[cmd_history.index] = string(rl_line_buffer);
    }

    if(counter<0){
        counter++;
    }
    if (cmd_history.index < (cmd_history.size - 1)){
        cmd_history.index++;
        string s;
        s = cmd_history.history[cmd_history.index];
        rl_replace_line(s.c_str(), 0);
        rl_redisplay();
        rl_point = rl_end;
    }
    else{
        if(cmd_history.index < cmd_history.size){
            cmd_history.index++;
        }
        rl_replace_line(last_cmd.c_str(), 0);
        rl_redisplay();
        rl_point = rl_end;
    }
    return 0;
}

int rl_beg_of_line(int count, int key)
{
    // cout << "hello" << endl;
    rl_point = 0;
    return 0;
}

int rl_end_of_line(int count, int key)
{
    // cout << "hello" << endl;
    rl_point = rl_end;
    return 0;
}

// initialize readline settings by mapping functions to keys
// basically the functions are triggered when the corresponding keys are pressed
void initialize_readline(){

    // map backward_history function to up arrow key
    // \e[A is the escape sequence for up arrow key]
    rl_bind_keyseq("\e[A", backward_history);

    // map forward_history function to down arrow key
    rl_bind_keyseq("\e[B", forward_history);

    // map rl_beg_of_line function to ctrl + a
    // \x01 is the escape sequence for ctrl + a
    rl_bind_key('\x01', rl_beg_of_line);

    // map rl_end_of_line function to ctrl + e
    // \x05 is the escape sequence for ctrl + e
    rl_bind_key('\x05', rl_end_of_line);

}

// function to count number of children of a process
int count_children(const pid_t pid) 
{
  DIR* proc_dir;
  int num_children = 0;

  if ((proc_dir = opendir("/proc"))) // Try to open the /proc directory which contains information about running processes
  {
    for (struct dirent* proc_id; (proc_id = readdir(proc_dir))!=NULL;) { // Read the /proc directory
      // The /proc directory contains other non-numeric entries (such as self, net, etc.), so we want to filter out everything except directories named by PIDs.
      if (is_number(proc_id->d_name)) // Check if the entry is a process ID
      {
        // The stat file for each process (located at /proc/[PID]/stat) contains information about the process, including its parent process ID.
        // The file is opened using ifstream (input file stream).
        ifstream ifs(string("/proc/" + string(proc_id->d_name) + "/stat").c_str()); // Open the stat file of the process
        string parent;
        for (int i = 0; i < 4; ++i) {
          ifs >> parent; // Only read the fourth field, which is the parent process ID
        }

        if (parent == to_string(pid)) { // Check if the parent process ID matches the given process ID
          ++num_children; // Increment the number of children
        }
      }
    }
    closedir(proc_dir);
    return num_children; 
  }
  perror("could not open directory");
  return -1;
}

// function to find time taken by a process
float time_taken(const pid_t pid)
{
  // Open the stat file of the process to read the start time of the process
  ifstream ifs(string("/proc/" + to_string(pid) + "/stat").c_str());
  string process_start, up_time;
  for(int i=0; i<22; i++){ // The start time of the process is the 22nd field in the stat file
    ifs >> process_start;
  }

  ifs.close();
  // System uptime is the time since the system was booted
  ifs.open(string("/proc/uptime").c_str()); // Open the uptime file to read the system uptime
  ifs >> up_time;
  // Calculate the time taken by the process as the difference between the system uptime and the process start time
  // HZ is the number of clock ticks per second
  // The time_taken is calculated in seconds
  float time_taken = stof(up_time) - (stof(process_start))/HZ; 
  return time_taken;
}

// function to find cpu usage of a process
float cpu_usage(const pid_t pid)
{
  // User time is the amount of time the process has spent in user mode
  // System time is the amount of time the process has spent in kernel mode

  // Open the stat file of the process to read the user and system time 
  ifstream ifs(string("/proc/" + to_string(pid) + "/stat").c_str());
  string u_time, s_time, temp;
  // Calculate the time taken by the process in seconds
  float process_elapsed = time_taken(pid);
  for(int i=0; i<22; i++){
    if(i==13){
      ifs >> u_time; // Read the user time (field 14)
    }
    else if(i == 14){
      ifs >> s_time; // Read the system time (field 15)
    }
    else{
      ifs >> temp; // Ignore the other fields
    }
  }
  // stof() converts the string to a float
  float process_usage = stof(u_time)/HZ + stof(s_time)/HZ;
  // CPU usage = time spent executing the process / total time elapsed since the process started
  return ((process_usage * 100)/ process_elapsed);
}

// function to find average cpu usage of children (all generations)
int find_avg_cpu_of_child(const pid_t pid, int depth)
{
  if(depth == MAX_DEPTH){
    return 0;
  }

  float first_gen = 0, num_children = 0;
  DIR* proc_dir;
  map<int,int> child_count;

  if((proc_dir = opendir("/proc"))) // Try to open the /proc directory which contains information about running processes
  {
    struct dirent* proc_id;
     for (proc_id; (proc_id = readdir(proc_dir));) // Read the /proc directory
     {
      // The /proc directory contains other non-numeric entries (such as self, net, etc.), so we want to filter out everything except directories named by PIDs.
      if (is_number(proc_id->d_name)) // Check if the entry is a process ID
      {
        ifstream ifs(string("/proc/" + string(proc_id->d_name) + "/stat").c_str()); // Open the stat file of the process
        string parent, status;
        for(int i=0; i<4; i++){
          if(i==2){
            ifs >> status; // Read the status of the process
          }
          ifs >> parent; // Read the parent process ID
        }
        if(parent == to_string(pid)) // Check if this process is a child of the given process
        {
          first_gen = cpu_usage(stoi(proc_id->d_name)); // Calculate the CPU usage of the child process
          float avg = find_avg_cpu_of_child(stoi(proc_id->d_name), depth+1); // Calculate the average CPU usage of the children of the child process
          if(avg != 0){
            first_gen += avg/ count_children(stoi(proc_id->d_name)); 
          }
          num_children++;
        }
      }
    }
  }
  closedir(proc_dir);
  return first_gen; 
}

// function to find heuristic value of a process
float heuristic(const pid_t pid){
  float heuristic = 0;
  // if count_children(pid) / time_taken(pid) is high, the process is likely to be a malware (creates many children in a short time)
  // if cpu_usage(pid) is high, the process is likely to be a malware (high CPU usage)
  // if find_avg_cpu_of_child(pid,0) is high, the process is likely to be a malware (high CPU usage of children (all generations))
  heuristic = count_children(pid)*60/time_taken(pid) + cpu_usage(pid) + find_avg_cpu_of_child(pid,0);
  return heuristic;
}

// function to find parent of a process
pid_t get_parent(const pid_t pid)
{
  // Read the parent process ID from the stat file of the process
  ifstream ifs(string("/proc/" + to_string(pid) + "/stat").c_str());
  string parent;
  for(int i=0; i < 4; i++)
  {
    ifs >> parent; // Read the fourth field, which is the parent process ID
  }
  ifs.close();
  return stoi(parent);
}

// Function to suggest the malware process recursively
pid_t suggestMalware(pid_t pid) {

  // Check the time spent and number of children of each process

  if (get_parent(pid) == 1){ // If the parent process is init, return the current process ID
    return pid;
  }

  pid_t parent_pid = get_parent(pid);
  float par_h = heuristic(parent_pid), h = heuristic(pid); // Calculate the heuristic values of the parent and child processes and compare them
  cout << "PARENT: "<< parent_pid << " Heuristic: " << par_h << "\n" << "CHILD: " << pid << " Heuristic: " << h <<"\n\n";
  if(par_h > h){
    suggestMalware(parent_pid); 
  }
  return pid; // Return the process ID with the highest heuristic value
}

// Function to traverse the process tree recursively and print all the ancestors of a process
void traverse(pid_t pid, int gen){
  if(pid == 1){
    cout << "No more parent process last process printed was init process\n";
    return;
  }
  pid_t parent_pid = get_parent(pid);
  cout << "Process ID: " << parent_pid << " Parent Generation " << gen <<"\n";
  traverse(parent_pid, gen+1);
}

// function to squash bug
void sb(int argc, char *argv[]) {
  
  pid_t pid;
  // argv[0] is the command itself
  // argv[1] is the process ID
  // argv[2] is the flag "-suggest"

  // Check if user provided a process ID
  if (argc < 2) 
  {
    cout << "Please provide a process ID." << endl;
  }

  // Check if user used the "-suggest" flag
  if (argc == 3 && string(argv[2]) == "-suggest") 
  { 
    // Get the process ID from the user
    pid = atoi(argv[1]);

    cout <<"Children: "  << count_children(pid) << "\n"; // Count the number of children of the given process
    cout << "cpu_usage: " << cpu_usage(pid) <<"\n"; // Calculate the CPU usage of the given process

    cout << "Current Process ID: " << pid << "\n";
    // Use the traverse function to display the parent, grandparent, and so on of the given process
    traverse(pid, 1);

    // Use the suggestMalware function to suggest the malware process
    pid_t malware = suggestMalware(pid);
    cout << "The expected malware Process ID is: " << malware << "\n";
  } 
  else 
  { // If the user did not use the "-suggest" flag
    // Get the process ID from the user
    pid = atoi(argv[1]);
    cout << "Children: " << count_children(pid) << "\n";
    // Use the traverse function to display the parent, grandparent, and so on of the given process
    cout << "Current Process ID: " << pid << "\n";
    traverse(pid,1);
  }
}

// function to get pid of the process that has the lock file open
void get_process_open_lock_file(char* filename, vector<int>* open_pids){

    /*
    The function get_process_open_lock_file is responsible for identifying which processes have a 
    specified file open and which processes have locked that file. It works by inspecting the 
    /proc directory (a Linux filesystem that contains information about running processes)
    and a separate lock file that contains details of file locks.
    */
   
    struct dirent *entry; // header file structure to store information about directory entries
    DIR *dp; // pointer to the directory stream
    int pid; // process ID

    dp = opendir(proc_path); // open the /proc directory
    if (dp == NULL) { // if the directory does not exist
        perror("opendir: Path does not exist or could not be read."); // print error message
        return;
    }

    // find all pids which have opened the file
    // Each subdirectory in /proc corresponds to a process, and the directory name is the process ID.
    while ((entry = readdir(dp))){ // read the /proc directory (list of running processes)

        if ((pid = atoi(entry->d_name)) == 0) // if the entry is not a process ID
            continue;

        // proc/<pid>/fd contains the file descriptors of the process
        string fd_path = string(proc_path) + string(entry->d_name) + string("/fd/"); // path to the file descriptor directory
        struct dirent *fd_entry; // header file structure to store information about directory entries
        DIR *fd_dp; // pointer to the directory stream

        fd_dp = opendir(fd_path.c_str()); // open the file descriptor directory
        if (fd_dp == NULL) // if the directory does not exist
            continue; 
        
        char buf[1024]; // buffer to store the file path
        short found = 0; // flag to indicate if the file is found

        while ((fd_entry = readdir(fd_dp))){ // read the file descriptor directory

            memset(buf, '\0', 1024); // fill the buffer with null characters
            int fd = atoi(fd_entry->d_name); // file descriptor

            // readlink - resolve the symbolic link and get the actual file path
            readlink((string(fd_path) + string(fd_entry->d_name)).c_str(), buf, 1024); 

            // strstr() - find the first occurrence of the substring in the buffer
            if (strstr(buf, filename) != NULL){ 

                found = 1; // set the flag
                break;
            }
        }
        if (found){ // if the file is found
            open_pids->push_back(pid); 
            // cout << pid << endl;
        }
        closedir(fd_dp); // close the file descriptor directory of the current running process in proc\
    }
    closedir(dp); // close the /proc directory

    // find all pids which have locked the file
    // lock file = /proc/locks contains information about file locks
    FILE *fp = fopen(lock_file, "r");
    if (fp == NULL){
        perror("fopen: Path does not exist or could not be read.");
        return;
    }

    char line[1024];
    while (fgets(line, 1024, fp) != NULL){ // read the lock file
        // strtok() splits the string according to the given delimiter
        char *token = strtok(line, " "); // split the lock file line into tokens
        int i = 0;
        while (token != NULL){ // for each token
            if (i == 4){ // pid is the 5th token in the line
                pid = atoi(token); 
                break;
            }
            token = strtok(NULL, " "); // iterate until the 5th token
            i++;
        }
        // for each pid that has locked some file, check if it is in the vector of pids that have opened the given file
        // if a process has a lock on a file and has also opened the given file, its negative pid is stored in the vector
        // If a process has opened the file but not locked any files, its positive pid is stored in the vector
        if (find(open_pids->begin(), open_pids->end(), pid) != open_pids->end()) // if pid is already in the vector
            replace(open_pids->begin(), open_pids->end(), pid, -pid); // replace pid with -pid
    }
}

// function to kill all processes that have the lock file open
void kill_processes(vector<int> pids){

    for (int i = 0; i < pids.size(); i++){
        // kill() is a system call that sends a signal to a process
        // SIGKILL: This signal forcefully terminates a process. It cannot be caught or ignored and
        // provides a reliable way for the OS or the user to terminate a process.
        kill(pids[i], SIGKILL);
        cout << "Killed process " << pids[i] << endl;
    }
}

// function to delete without prejudice
void delep(char* file)
{
    /*
    The delep function is designed to identify processes that have a specific file open (and possibly locked), 
    then offer the user the option to kill those processes before attempting to delete the file.
    */
    char* filename = file;
    pid_t pfd[2];

    // pipe the pfd array
    pipe(pfd); // Create a pipe to communicate with the child process
    pid_t pid = fork(); // Create a child process

    if (pid == 0){ // Child process

        vector<int> pids; // stores pids of processes that have opened the given file
        get_process_open_lock_file(filename, &pids); // get pids of processes that have opened the file
        // If a process has a lock on a file and has also opened the given file, its negative pid is stored in the vector

        size_t len;
        len = pids.size();
        // child process writes the number of PIDs and each PID itself to the pipe:
        // write pid vector to parent process using pipe
        write(pfd[1], &len, sizeof(len));

        int p;
        for (int i = 0; i < len; i++)
        {
            p = pids[i];
            write(pfd[1], &p, sizeof(pid_t));
        }

        exit(0); // Exit the child process
    }
    wait(NULL); // Wait for the child process to finish

    size_t open_len;
    int p_;
    vector<int> open_pids;
    vector<int> locked_pids;

    // read total number of PIDs from the pipe
    read(pfd[0], &open_len, sizeof(open_len));

    for (int i = 0; i < open_len; i++){
        // read each PID from the pipe
        read(pfd[0], &p_, sizeof(pid_t));
        if (p_ < 0) // if the PID is negative, it means the process has locked file(s)
        {
            // store in both vectors
            open_pids.push_back(-p_);
            locked_pids.push_back(-p_); 
        }
        else
            open_pids.push_back(p_);
    }
    /
        / Display results to the user
    cout << "PIDs which have opened the file:" << endl << endl;
    for (int i = 0; i < open_pids.size(); i++)
        cout << open_pids[i] << endl;

    cout << "PIDs which have locked the file:" << endl << endl;
    for (int i = 0; i < locked_pids.size(); i++)
        cout << locked_pids[i] << endl;

    // Ask the user
    cout << "Do you want to kill these processes? (y/n): ";
    char c;
    cin >> c;

    if (c == 'y'){
        // Kill the processes
        cout << "Killing processes..."<< endl;
        kill_processes(open_pids);

        // Try to delete the file after killing the processes that have it open
        // remove() is a system call that deletes a file
        if (remove(filename) == 0)
            cout << "File deleted successfully" << endl;
        else
            cout << "Error: unable to delete the file" << endl;
    }
}
