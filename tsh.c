/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 	name : RUTVIK MUKESHBHAI MAVANI
 	ID   : 201601152
 	email: 201601152@daiict.ac.in
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */


/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
	char* argv[MAXARGS];
	int bg = parseline(cmdline,argv);
	if(argv[0] == NULL) return;
	if(!builtin_cmd(argv))
	{
		int cpid;
		sigset_t mask;		/*procedure to blcok child process*/
		sigemptyset(&mask);
		sigaddset(&mask,SIGCHLD);
		sigprocmask(SIG_BLOCK,&mask,NULL);
		if((cpid = fork()) == 0)
		{
			if(setpgid(0,0) < 0)	/*change default group id to process id*/
			{
				perror(argv[0]);	/*error checking*/
				exit(-1);
			}
			if(sigprocmask(SIG_UNBLOCK,&mask,NULL) < 0) /*unblock sigchild*/
			{
				perror(argv[0]);		/*error checking*/
				exit(-1);
			}
			if(execvp(argv[0],argv) < 0)	/*error in exec*/
			{
				if(errno == ENOENT) printf("%s: Command not found\n",argv[0]);		/*programme doesn't exist*/
				else perror(argv[0]);												/*other errors*/
				exit(-1);
			}
		}
		if(cpid != -1)									/*if fork is successful*/
		{
			addjob(jobs,cpid,(bg == 0?FG:BG),cmdline);	
			sigprocmask(SIG_UNBLOCK,&mask,NULL);		/*unblock SIGCHLD after taking care of race condition*/
			if(!bg)										/*if foreground process then wait*/
			{
				waitfg(cpid);
			}
			else										/*if background process then print description but don't wait*/
			{
				struct job_t *j = getjobpid(jobs,cpid);
				printf("[%d] (%d) %s",j->jid,j->pid,j->cmdline);
				fflush(stdout);
			}
		}
	}
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	if(strcmp(argv[0],"quit") == 0)	
	{
		int i;
		for(i=0;i < MAXJOBS;i++)
		{
			if(jobs[i].state == ST)								/*checking that any stopped process*/
			{
				printf("There are stopped process !!!\n");
				return 1;										/*if there is stopped process than don't exit just return*/
			}
		}
		exit(0);
	}
	if(strcmp(argv[0],"jobs") == 0)
	{
		listjobs(jobs);											/*just print all jobs*/
		return 1;
	}
	if(strcmp(argv[0],"fg") == 0 || strcmp(argv[0],"bg") == 0)
	{
		do_bgfg(argv);											/*handle foreground background process*/
		return 1;
	}
    return 0;     /* not a builtin command */
}

int isValidArg(char* str,int len) /* return 0 if not valid, return 1 if job, return 2 if pid. */
{
	if(str[0] == '%')
	{
		if(len==1)
		return 0;				/*if first character is '%' and no number behind if than return invalid*/
		int i;
		for(i=1;i<len;i++)
		if(str[i] < '0' || str[i] > '9')	/*if string consist of charecter which is representing number then return invalid*/
		return 0;
		return 1;
	}
	else if(str[0] >= '0' && str[0] <= '9')
	{
		int i;
		for(i=1;i<len;i++)
		if(str[i] < '0' || str[i] > '9')	/*if string consist of charecter which is representing number then return invalid*/
		return 0;
		return 2;
	}
	else return 0;
}
int str2num(char* str,int len) /*convert pid or jid into number and take care of %*/
{
	int num = 0;
	int factor = 1;
	int i;
	for(i=len-1;i>=1;i--)
	{
		num = num + (str[i] - '0')*factor;
		factor *= 10;
	}
	if(str[0] != '%') num = num + (str[0] - '0')*factor;
	return num;
}
/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
	if(argv[1] == NULL)		/*if jid or pid is missing*/
	{
		printf("%s command requires PID or %%jobid argument\n",argv[0]);
		return;
	}
	int len = strlen(argv[1]);
	int valid = isValidArg(argv[1],len);	/*check if it is valid argument*/
	if(valid == 0)			/*if jid or pid is not number*/
	{
		printf("%s : argument must be a PID or %%jobid\n",argv[0]);
		return;
	}
	
	int num = str2num(argv[1],len);
	struct job_t* j;
	if(valid == 1)
	{
		j = getjobjid(jobs,num);
		if(j == NULL)		/*if specified jid does't exist in job table*/
		{
			printf("%s: No such job\n",argv[1]);
			return;
		}
	}
	else
	{
		j = getjobpid(jobs,num);
		if(j == NULL)		/*if specified pid does't exist in job table*/
		{
			printf("(%d): No such process\n",num);
			return;
		}
	}
	int isbg = (strcmp(argv[0],"bg")==0?1:0);	/*flag for bg or fg*/
	
	if(j->state == ST && isbg == 1)			/*if stopped and do background*/
	{
		j->state = BG;						/*change the state in jobtable*/
		killpg(j->pid,SIGCONT);				/*continuing all process in specified process group*/
		printf("[%d] (%d) %s",j->jid,j->pid,j->cmdline);
		fflush(stdout);
	}
	else if(j->state == ST && isbg == 0)	/*if stopped and do foreground*/
	{
		j->state = FG;						/*change the state in jobtable*/
		killpg(j->pid,SIGCONT);				/*continuing all process in foreground in specified process group*/
		waitfg(j->pid);						/*wait for foreground processes*/
	}
	else if(j->state == BG && isbg == 0)	/*if background and do foreground*/
	{
		j->state = FG;						/*change the state in jobtable*/
		waitfg(j->pid);						/*wait for foreground processes*/
	}
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	struct job_t* j = getjobpid(jobs,pid);
	while(j->state == FG)		/*while state is foreground keep checking*/
	{
		sleep(1);
	}
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
	pid_t pid;
	int status;
	while((pid = waitpid(-1,&status,WNOHANG | WUNTRACED)) > 0)		/*while there exist terminated or stopped child process*/
	{
		if(WIFEXITED(status)) deletejob(jobs,pid);					/*if process has normally exited then delete entry from job table*/
		else if(WIFSIGNALED(status))								/*if process is signaled than print signal number*/
		{
			int jid = pid2jid(pid);
			deletejob(jobs,pid);
			printf("Job [%d] (%d) terminated by signal %d\n",jid,pid,WTERMSIG(status));
		}
		else if(WIFSTOPPED(status))									/*is process is stopped then change state and print job id*/
		{
			struct job_t* j = getjobpid(jobs,pid);
			j->state = ST;
			printf("Job [%d] (%d) stopped by signal %d\n",j->jid,pid,WSTOPSIG(status));
		}
	}
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
	pid_t fg_pid = fgpid(jobs);		/*get pid of foregroung process jobs*/
	if(fg_pid == 0) return;			/*if foreground process if doesn't exist*/
	if(killpg(fg_pid,sig) < 0)		/*send sigint to process group*/
	{
		perror("./tsh");			/*error in system call*/
		return;
	}
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
	pid_t fg_pid = fgpid(jobs);		/*get pid of foregroung process jobs*/
	if(fg_pid == 0) return;			/*if foreground process if doesn't exist*/
	if(killpg(fg_pid,sig) < 0)		/*send sigint to process group*/
	{
		perror("./tsh");			/*error in system call*/
		return;
	}
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



