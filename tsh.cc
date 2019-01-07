// 
// tsh - A tiny shell program with job control
// 
// Nikolai Alexander - nial3328
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

// READ CHAPTER 8!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Pay close attention to the hints in the assignment writeup

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
// -- Based off page 755 in the textbook--
// HINT: the parent must use sigprocmask to block SIGCHLD signals before it forks the child,
// and then unblock these signals, again using sigprocmask after it adds the child to the job
// list by calling addjob. Since children inherit the blocked vectors of their parents, the
// child must be sure to then unblock SIGCHLD signals before it execs the new program.
void eval(char *cmdline) 
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];
  pid_t pid; // So we can check if we are looking at parents or children
  sigset_t mask; // Mask to use in sigprocmask in order to block SIGCHLD


  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); 
  if (argv[0] == NULL)  
    return;   /* ignore empty lines */

  if (!builtin_cmd(argv)){
  	// Block SIGCHLD signals before forking the child
  	sigprocmask(SIG_BLOCK, &mask, NULL);

  	// If our fork returns 0, then we are in a child process
  	if ((pid = fork()) == 0){
  		// If it is a child pid, assign it its own group id to avoid recieving a
  		// SIGINT/SIGSTP when typing ctrl-c/ctrl-z
  		setpgid(0,0);

  		// Unblock SIGCHLD before executing the new program
  		sigprocmask(SIG_UNBLOCK, &mask, NULL);
  		// sigIf the command does not exist return a message
  		// printf("We are about to run execv\n");
		if(execvp(argv[0], argv) < 0) {
			printf("%s: Command not found \n", argv[0]);
			// printf("About to exit");
			exit(0);
		}
  	}
  	else {
  	// If the parent is a foreground job, wait for the current foreground job to terminate
	  	if(!bg){
	  		// Add job to the foreground job list
	  		addjob(jobs, pid, FG, cmdline);
	  		// printf("Added job to list\n");
	  		sigprocmask(SIG_UNBLOCK, &mask, NULL);
	  		// Wait for current foreground job to terminate (based off waitpid)
	  		// printf("Now entering waitfg in eval\n");
	  		waitfg(pid);
	  		// printf("Successfuly exited waitfg\n");
	  	}
	  	else {
	  		// Add job to the background job list
	  		addjob(jobs, pid, BG, cmdline);

	  		// Print jobid, pid, and the command
	  		printf("[%d] (%d) %s",getjobpid(jobs,pid)->jid, pid, cmdline);


	  	}
	  	sigprocmask(SIG_UNBLOCK, &mask, NULL);
  }
  }
  return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
  string cmd(argv[0]);

  //  Quit program
  if(cmd == "quit") {
  	exit(0);
  }

  // List jobs
  if(cmd == "jobs"){
  	listjobs(jobs);
  	return 1;	// Return True
  }

  if(cmd == "bg" || cmd=="fg"){
  	do_bgfg(argv);
  	return 1;
  }

  if(cmd == "&"){
  	return 1;
  }

  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  struct job_t *jobp=NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //

  // Hints: 
  // 	The bg <job> command restarts <job> by sending it a SIGCONT signal, and
  // 	then runs it in the background. The <job> argument can be either a PID
  // 	or a JID.
  // 	The fg <job> command restarts <job> by sending it a SIGCONT signal, and
  // 	then runs it in the foreground. The <job> argument can be either a PID
  // 	or a JID.
  string cmd(argv[0]);

  // Continue the process before changing the state of the stopped job
  kill(-jobp->pid, SIGCONT);
  if(cmd == "bg"){
  	// 'job.state' located in job_t struct in 'jobs.h'
  	jobp->state = BG; // Change the state of the job to the background
  	printf("[%d] (%d) %s",jobp->jid, jobp->pid, jobp->cmdline);
  }
  else if (cmd == "fg"){
  	// Change the state of the job to the foreground and wait until the current
  	// foreground process terminates before we run it
  	jobp->state = FG;
  	// printf("Now entering waitfg in do_bgfg\n");
  	waitfg(jobp->pid);
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
// Hint:
// 	In waitfg, use a busy loop around the sleep function.
void waitfg(pid_t pid)
{
	// Busy loop to iterate around the sleep function until
	// 'fgpid' - Return PID of current foreground job, 0 if no such job
	while(pid == fgpid(jobs)){ // 'fgpid' in 'jobs.cc' line 87
		// printf("Sleeping....\n");
		sleep(1);
	}

	// printf("Leaving waitfg\n");
	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
// HINTS:
//   One of the tricky parts of the assignment is deciding on the allocation of
//   work between the waitfg and sigchld handler functions. We recommend the 
//   following approach:
//      – In waitfg, use a busy loop around the sleep function.
//      – In sigchld handler, use exactly one call to waitpid.
//   While other solutions are possible, such as calling waitpid in both waitfg and
//   sigchld handler, these can be very confusing. It is simpler to do all reaping
//   in the handler.
// 
//   The waitpid, kill, fork, execve, setpgid, and sigprocmask functions will come
//   in very handy. The WUNTRACED and WNOHANG options to waitpid will also be
//   useful. These are described in detail in the text. (pp. 744-745)
//                ** WAITPID EXAMPLES PP. 747 & 749 **
//   Modifying Default Behavior:
//     - WNOHANG: Return 0 if none of the child processes in the wait set has
//                terminated,
//         * Default behavior suspends calling the process until child terminates
//     - WUNTRACED: Suspends execution of the calling process until a process in
//                  the wait set becomes terminated or stopped
//         * Returns pid of terminated/stopped child that caused the return
//     - WNOHANG | WUNTRACED: Return with...
//                              1) 0 if none of the children in the wait set have
//                                 stopped/terminated
//                              2) pid of one of the stopped/terminated children
//   Checking Exit Status of Reaped Child:
//     - WIFEXITED(status): Returns TRUE of child terminated normally via exit or
//                          return.
//     - WEXITSTATUS(status): Returns the exited status of a normally terminated
//                            child
//         * Only works if WIFEXITED returns TRUE
//     - WIFSIGNALED(status): Returns TRUE if the child process terminated because
//                            of a signal that was NOT caught
//     - WTERMSIG(status): Returns the signal value that caused the child to
//                        terminate
//         * Only works if WIFSIGNALED returns TRUE
//     - WIFSTOPPED(status): Returns TRUE if the child that caused the return
//                           is currently stopped
//     - WSTOPSIG(status): Returns the signal value that caused the child to
//                         stop
//        * Only works if WIFSTOPPED returns TRUE
//     - WIFCONTINUED(status): Returns TRUE if the child process was restarted
//                             by a SIGCONT signal
void sigchld_handler(int sig) 
{
	// printf("Entering sigchld_handler\n");
	int status;
	pid_t pid;

	// Need to call waitpid inside the loop so it can continuously be called
	// When it equals 0, none of the children in the wait set have stopped/
	// terminated (WNOHANG)
	// Error Condition: If the calling process has no children, then waitpid
	// returns -1
	// If the pid (first argument) is -1, then the wait set consists of all
	// the parent's child processes
	while((pid = waitpid(-1,&status, WNOHANG | WUNTRACED)) > 0){
		// printf("waitpid checked\n");
		// Need to check WIFEXITED / WIFSIGNALLED / WIFSTOPPED in order to 
		// check if the process was terminated/stopped and if it was terminated
		// how it was terminated.
		if(WIFEXITED(status) == true){
			// printf("Process Terminated Normally\n");
			// deletejob - Delete a job whose PID=pid from the job list (jobs.cc - l. 69)
			deletejob(jobs,pid);
		}
		else if(WIFSIGNALED(status) == true){
			// printf("Process Terminated, Signal not caught\n");
			struct job_t *jobp= getjobpid(jobs,pid);
			// pid2jid - Map process ID to job ID (jobs.cc - l. 122)
			printf("Job [%d] (%d) terminated by signal %d\n", jobp->jid, pid, WTERMSIG(status));
			deletejob(jobs,pid);
		}
		else if(WIFSTOPPED(status) == true){
			  // printf("Process Stopped");
			  struct job_t *jobp= getjobpid(jobs,pid);
			  printf("Job [%d] (%d) stopped by signal %d\n", jobp->jid, pid, WSTOPSIG(status));
			  jobp->state = ST; // Set the state of the job to stopped
		}
	}

	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
// 
// Hints for sigint_handler and sigstp_handler:
// 	When you implement your signal handlers, be sure to send SIGINT and SIGTSTP
//  signals to the entire foreground process group, using ”-pid” instead of ”pid” 
//  in the argument to the kill function. The sdriver.pl program tests for this
//  error.
// 
//  When you run your shell from the standard Unix shell, your shell is running
//  in the foreground process group. If your shell then creates a child process, 
//  by default that child will also be a member of the foreground process group. 
//  Since typing ctrl-c sends a SIGINT to every process in the foreground group, 
//  typing ctrl-c will send a SIGINT to your shell, as well as to every process
//  that your shell created, which obviously isn’t correct.
void sigint_handler(int sig) 
{
  // printf("Ctrl-C signal caught\n");
  pid_t pid;
  // SIGINT is caught by Signal(SIGINT,  sigint_handler) on line 90, and is sent
  // to this function where we then check to see if it is a foreground job
  
  // If pid is 0, that means there is no foreground job. Since we can only have
  // one foreground job running at a time, it will kill the foreground job we are
  // looking for.
  if ((pid = fgpid(jobs)) != 0){
  	// In our case, sig is equal to SIGINT, which sends an interrupt to the
  	// foreground pid with '-pid'.
  	kill(-pid,sig);
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
  // printf("Ctrl-Z signal caught\n");
  pid_t pid;

  // This is the same exact steps as sigint_handler. SIGSTP is caught by 
  // Signal(SIGTSTP, sigtstp_handler) on line 91, and is sent to this function
  // We then use kill with SIGSTP to stop the program until SIGCONT is called.
    if ((pid = fgpid(jobs)) != 0){
  	// In our case, sig is equal to SIGSTP, which sends an interrupt to the
  	// foreground pid with '-pid'.
  	kill(-pid,sig);
  }
  return;
}

/*********************
 * End signal handlers
 *********************/




