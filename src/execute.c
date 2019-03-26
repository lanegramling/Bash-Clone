/**
 * @file execute.c
 *
 * @brief Implements interface functions between Quash and the environment and
 * functions that interpret an execute commands.
 *
 * @note As you add things to this file you may want to change the method signature
 */
 //

#include "execute.h"

#include <stdio.h>

#include <unistd.h>

#include <string.h>

#include <signal.h>

#include <sys/wait.h>

#include "quash.h"

#include "deque.h"

//global variable to allow pipe reuse
static int pipes[2][2];
static int prev = -1;
static int next = 0;

//pid_t deque for use with Jobs
IMPLEMENT_DEQUE_STRUCT(PIDDQ, pid_t);
IMPLEMENT_DEQUE(PIDDQ, pid_t);

//Job struct & DEque
typedef struct Job {
    int job_id;
    char* cmd;
    PIDDQ pids;
} Job;
IMPLEMENT_DEQUE_STRUCT(JobDQ, Job);
IMPLEMENT_DEQUE(JobDQ, Job);

JobDQ jobs; //DEQue for job management.

//Initialize a new job object
Job newJob (char* cmd) {
    return (Job) {
        (is_empty_JobDQ(&jobs)) ? 1 : peek_back_JobDQ(&jobs).job_id + 1,
        cmd,
        new_PIDDQ(1)
    };
}

//Job Destructor
void destroyJobs(Job j) {
    destroy_PIDDQ(&j.pids);
    if (j.cmd) free(j.cmd);
}

//print_job wrapper for Job struct
void printJob(Job j) {
    print_job(j.job_id, peek_front_PIDDQ(&j.pids), j.cmd);
}


/***************************************************************************
 * Interface Functions
 ***************************************************************************/

// Return a string containing the current working directory.
char* get_current_directory(bool* should_free) {
  // Get the current working directory. This will fix the prompt path.
  //As according to documentation for getcwd() function
  //Return a pointer to an array of the absolute pathname of the cwd
  long size;  //path size, required for getcwd()
  char *buf;  //array of pathname, required for getcwd()
  char *ptr;  //pointer to pathname array

  size = pathconf(".", _PC_PATH_MAX);

  if ((buf = (char *)malloc((size_t)size)) != NULL) ptr = getcwd(buf, (size_t)size);
  else perror("getcwd() error");

  // TODO: Change this to true if necessary
  *should_free = false;

  return ptr;
}

// Returns the value of an environment variable env_var
const char* lookup_env(const char* env_var) {
    return getenv(env_var);
}

// Check the status of background jobs
void check_jobs_bg_status() {
  // TODO: Check on the statuses of all processes belonging to all background
  // jobs. This function should remove jobs from the jobs queue once all
  // processes belonging to a job have completed.

  // TODO: Once jobs are implemented, uncomment and fill the following line
  // print_job_bg_complete(job_id, pid, cmd);

      // if (!is_empty_JobDQ(&jobs)) {
      //     Job j = pop_front_JobDQ(&jobs);
      //     int status;
      //     pid_t wp;
      //     if (!is_empty_PIDDQ(&j.pids))
      //         for (pid_t p = pop_front_PIDDQ(&j.pids); !is_empty_PIDDQ(&j.pids); p = pop_front_PIDDQ(&j.pids))
      //             if ((wp = waitpid(p, &status, 0)) == -1) perror("waitpid() error");
      //             else if (wp == 0) push_back_PIDDQ(&j.pids, p); //Stil waiting throw it back in the job's PIDDQ
      //             else if (wp == p) exit(EXIT_SUCCESS); //Complete
      // }

}

// Prints the job id number, the process id of the first process belonging to
// the Job, and the command string associated with this job
void print_job(int job_id, pid_t pid, const char* cmd) {
  printf("[%d]\t%8d\t%s\n", job_id, pid, cmd);
  fflush(stdout);
}

// Prints a start up message for background processes
void print_job_bg_start(int job_id, pid_t pid, const char* cmd) {
  printf("Background job started: ");
  print_job(job_id, pid, cmd);
}

// Prints a completion message followed by the print job
void print_job_bg_complete(int job_id, pid_t pid, const char* cmd) {
  printf("Completed: \t");
  print_job(job_id, pid, cmd);
}

/***************************************************************************
 * Functions to process commands
 ***************************************************************************/
// Run a program reachable by the path environment variable, relative path, or
// absolute path
void run_generic(GenericCommand cmd) {
  // Execute a program with a list of arguments. The `args` array is a NULL
  // terminated (last string is always NULL) list of strings. The first element
  // in the array is the executable
  char* exec = cmd.args[0];
  char** args = cmd.args;
  execvp(exec, args);

  perror("ERROR: Failed to execute program");
}

// Print strings
void run_echo(EchoCommand cmd) {
  // Print an array of strings. The args array is a NULL terminated (last
  // string is always NULL) list of strings.
  char** str = cmd.args;
  for (; *str; str++) printf("%s ", *str);
  if (str - 1) printf("\n");

  // Flush the buffer before returning
  fflush(stdout);
}

// Sets an environment variable
void run_export(ExportCommand cmd) {
  // Write an environment variable
  const char* env_var = cmd.env_var;
  const char* val = cmd.val;
  setenv(env_var, val, 1);
}

// Changes the current working directory
void run_cd(CDCommand cmd) {
  // Get the directory name
  const char* dir = cmd.dir;

  // Check if the directory is valid
  if (dir == NULL) { perror("ERROR: Failed to resolve path"); return; }
  else {
    chdir(dir);

    // Update PWD/OLDPWD env_vars
    setenv("OLDPWD", getenv("PWD"), 1);
    setenv("PWD", dir, 1);
  }

}

// Sends a signal to all processes contained in a job
void run_kill(KillCommand cmd) {
  int signal = cmd.sig;
  int job_id = cmd.job;

  kill(job_id, signal);
  //if pid = 0, kill() sends signal to sender process ID
}

// Prints the current working directory to stdout
void run_pwd() {

  char cwd[1024];
  getcwd(cwd, sizeof(cwd));
  printf(cwd);
  printf("\n");

  // Flush the buffer before returning
  fflush(stdout);
}

// Prints all background jobs currently in the job list to stdout
void run_jobs() {
  // TODO: Print background jobs
  void (*printPtr)(Job) = printJob;
  apply_JobDQ(&jobs, printPtr);

  // Flush the buffer before returning
  fflush(stdout);
}

/***************************************************************************
 * Functions for command resolution and process setup
 ***************************************************************************/

/**
 * @brief A dispatch function to resolve the correct @a Command variant
 * function for child processes.
 *
 * This version of the function is tailored to commands that should be run in
 * the child process of a fork.
 *
 * @param cmd The Command to try to run
 *
 * @sa Command
 */
void child_run_command(Command cmd) {
  CommandType type = get_command_type(cmd);

  switch (type) {
  case GENERIC:
    run_generic(cmd.generic);
    break;

  case ECHO:
    run_echo(cmd.echo);
    break;

  case PWD:
    run_pwd();
    break;

  case JOBS:
    run_jobs();
    break;

  case EXPORT:
  case CD:
  case KILL:
  case EXIT:
  case EOC:
    break;

  default:
    fprintf(stderr, "Unknown command type: %d\n", type);
  }
}

/**
 * @brief A dispatch function to resolve the correct @a Command variant
 * function for the quash process.
 *
 * This version of the function is tailored to commands that should be run in
 * the parent process (quash).
 *
 * @param cmd The Command to try to run
 *
 * @sa Command
 */
void parent_run_command(Command cmd) {
  CommandType type = get_command_type(cmd);

  switch (type) {
  case EXPORT:
    run_export(cmd.export);
    break;

  case CD:
    run_cd(cmd.cd);
    break;

  case KILL:
    run_kill(cmd.kill);
    break;

  case GENERIC:
  case ECHO:
  case PWD:
  case JOBS:
  case EXIT:
  case EOC:
    break;

  default:
    fprintf(stderr, "Unknown command type: %d\n", type);
  }
}

/**
 * @brief Creates one new process centered around the @a Command in the @a
 * CommandHolder setting up redirects and pipes where needed
 *
 * @note Processes are not the same as jobs. A single job can have multiple
 * processes running under it. This function creates a process that is part of a
 * larger job.
 *
 * @note Not all commands should be run in the child process. A few need to
 * change the quash process in some way
 *
 * @param holder The CommandHolder to try to run
 *
 * @sa Command CommandHolder
 */
void create_process(CommandHolder holder) {
  // Read the flags field from the parser
  bool p_in  = holder.flags & PIPE_IN;
  bool p_out = holder.flags & PIPE_OUT;
  bool r_in  = holder.flags & REDIRECT_IN;
  bool r_out = holder.flags & REDIRECT_OUT;
  bool r_app = holder.flags & REDIRECT_APPEND; // This can only be true if r_out
                                               // is true

  // TODO: Remove warning silencers
  (void) r_in;  // Silence unused variable warning
  (void) r_out; // Silence unused variable warning
  (void) r_app; // Silence unused variable warning

  //Create pipe only if sending output
  //(pipes for receiving input are created here first)
  if(p_out) pipe(pipes[next]);

  pid_t childProcess;

  childProcess = fork();
  if (childProcess == 0) {
    //Set up necessary pipes for read/write before running child command
    if (p_in) { dup2 (pipes[prev][0], STDIN_FILENO); close (pipes[prev][1]); }
    if (p_out) { dup2 (pipes[next][1], STDIN_FILENO); close (pipes[next][0]); }
    child_run_command(holder.cmd);
    exit(EXIT_SUCCESS);
  }
  else {
    //Push job to our JobDQ with its PID
    // Job j = newJob(get_command_string());
    // push_back_PIDDQ(&j.pids, childProcess);
    // push_back_JobDQ(&jobs, j);

    //we are in a parent, no prev pipes
    if (p_out) close (pipes[next][1]); //Close open pipe from parent write
    parent_run_command(holder.cmd);

    //Shift pipes for reuse
    next = (next + 1) % 2;
    prev = (prev + 1) % 2;

  }
}

// Run a list of commands
void run_script(CommandHolder* holders) {
  if (holders == NULL)
    return;

    //Initialize JobDQ with destructor
    // jobs = new_destructable_JobDQ(1, destroyJobs);

    check_jobs_bg_status();

  if (get_command_holder_type(holders[0]) == EXIT &&
      get_command_holder_type(holders[1]) == EOC) {
    end_main_loop();
    return;
  }

  CommandType type;

  // Run all commands in the `holder` array
  for (int i = 0; (type = get_command_holder_type(holders[i])) != EOC; ++i)
    create_process(holders[i]);

  if (wait(NULL) == -1) {exit(EXIT_FAILURE);} //For IO cleanliness

  if (!(holders[0].flags & BACKGROUND)) {
    // Not a background Job
    // Wait for all processes under the job to complete
    // if (!is_empty_JobDQ(&jobs)) {
    //     Job j = pop_front_JobDQ(&jobs);
    //     int status;
    //     pid_t wp;
    //     if (!is_empty_PIDDQ(&j.pids))
    //         for (pid_t p = pop_front_PIDDQ(&j.pids); !is_empty_PIDDQ(&j.pids); p = pop_front_PIDDQ(&j.pids))
    //             if ((wp = waitpid(p, &status, 0)) == -1) perror("waitpid() error");
    //             else if (wp == 0) push_back_PIDDQ(&j.pids, p); //Stil waiting throw it back in the job's PIDDQ
    //             else if (wp == p) exit(EXIT_SUCCESS); //Complete
    // }
  } else {
    // A background job.  Move forward.
    // Job j = newJob(get_command_string());
    // pid_t proc;
    // push_back_JobDQ(&jobs, j);
    // push_back_PIDDQ(&j.pids, proc);
    // print_job_bg_start(j.job_id, peek_front_PIDDQ(&j.pids), j.cmd);
  }
}
