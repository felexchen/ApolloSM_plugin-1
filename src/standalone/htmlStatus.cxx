#include <stdio.h>
#include <ApolloSM/ApolloSM.hh>
#include <vector>
#include <string>
#include <signal.h>
#include <sys/types.h> //umask
#include <sys/stat.h> // umask

//TCLAP parser
#include <tclap/CmdLine.h>

// ====================================================================================================
// Kill program if it is in background
bool static volatile loop;
void static signal_handler(int const signum) {
  if(SIGINT == signum) {
    loop = false;
  }
}

// ====================================================================================================

int main(int argc, char** argv) {

  std::string file;
  size_t verbosity;
  std::string type;
  std::string connection_file;

  try {
    TCLAP::CmdLine cmd("Apollo XVC.",
		       ' ',
		       "XVC");
    TCLAP::ValueArg<std::string> conn_file("c", //one char flag
					  "connection_file", // full flag name
					  "connection file", //description
					  true, //required
					  std::string(""), //Default
					  "string", //type
					  cmd);


    TCLAP::ValueArg<std::string> filename("f", //one char flag
					  "file", // full flag name
					  "HTML table file", //description
					  true, //required
					  std::string("index.html"), //Default
					  "string", //type
					  cmd);

    TCLAP::ValueArg<size_t> level("l", //one char flag
				  "verbosity", //full flag name
				  "level sets verbosity for the HTML table, 1-9", //description
				  false, //Not Required
				  1, //Default
				  "size_t", //type
				  cmd);

    TCLAP::ValueArg<std::string> bare("t", //one char flag
				      "type", //full flag name
				      "Option to set HTML as bare", //description
				      false, //Not required 
				      std::string("HTML"), //Default
				      "string", //type
				      cmd);
    
    //Parse the command line arguments
    cmd.parse(argc, argv);
    file = filename.getValue();
    verbosity = level.getValue();
    type = bare.getValue();
    connection_file = conn_file.getValue();
    fprintf(stderr, "running: verbosity=%d filename=\"%s\"\n", verbosity, file.c_str());
    

  }catch (TCLAP::ArgException &e) {
    fprintf(stderr, "Failed to Parse Command Line, running default: verbosity=1 filename=\"index.html\"\n");
    return -1;
  }

  printf("1\n");
  //Create ApolloSM class
  ApolloSM * SM = NULL;
  printf("1\n");  
  SM = new ApolloSM();
  printf("1\n");
  std::vector<std::string> arg;
  printf("1\n");
  arg.push_back(connection_file);
  printf("1\n");
  //  printf("the connection file is %s\n", arg[0].c_str());
  SM->Connect(arg);
  printf("1\n");
  std::string strOut;
  //Generate HTML Status
  
  printf("Daemon\n");

/*
  // ==================================================
  // Daemon
  pid_t pid, sid;
  pid = fork();
  if(0 > pid) {
    // Something went wrong
    // log something
    exit(EXIT_FAILURE);
  } else if(0 < pid) {
    // We are the parent and created a child with pid pid
    FILE * pidFile = fopen("/var/run/htmlStatus.pid","w");
    fprintf(pidFile,"%d\n",pid);
    fclose(pidFile);
    exit(EXIT_SUCCESS);
  } else {
    // I'm the child!
  }

  // Change the file mode mask to allow read/write
  umask(0);

  // Create log file
  FILE * logFile = fopen("/var/log/htmlStatus.log","w");
  if(NULL == logFile) {
    fprintf(stderr,"Failed to create log file for htmlStatus\n");
    exit(EXIT_FAILURE);
  }
  fprintf(logFile,"Opened log file\n");
  fflush(logFile);

  // create new SID for daemon
  sid = setsid();
  if(0 > sid) {
    fprintf(logFile,"Failed to change SID\n");
    fflush(logFile);
    exit(EXIT_FAILURE);
  } else {
    fprintf(logFile, "Set SID to %d\n",sid);
    fflush(logFile);
  }

  // Move to /tmp/bin
  if(0 > (chdir("/tmp/bin"))) {
    fprintf(logFile,"Failed to change path to /tmp/bin \n");
    fflush(logFile);
    exit(EXIT_FAILURE);
  } else {
    fprintf(logFile, "Changed path to /tmp/bin \n");
    fflush(logFile);
  }

  // Everything looks good, close standard file fds.
  // Daemons don't use them, so close for security purposes
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
*/

  // ==================================================
  // Signal handling
  struct sigaction sa_INT,sa_TERM,oldINT_sa,oldTERM_sa;
  memset(&sa_INT,0,sizeof(sa_INT)); // clear struct
  memset(&sa_TERM,0,sizeof(sa_TERM)); // clear struct
  // apply the action
  sa_INT.sa_handler = signal_handler;
  sa_TERM.sa_handler = signal_handler;
  sigemptyset(&sa_INT.sa_mask);
  sigemptyset(&sa_TERM.sa_mask);
  sigaction(SIGINT, &sa_INT, &oldINT_sa);
  sigaction(SIGTERM, &sa_TERM, &oldTERM_sa);
  loop = true;
  
  // ==================================================

  while(loop) {
    strOut = SM->GenerateHTMLStatus(file, verbosity, type);
    usleep(30*1000000); // 30 seconds
  }
  
  // Restore old actions of receiving SIGINT and SIGTERM (which is to kill program) before returning
  sigaction(SIGINT, &oldINT_sa, NULL);
  sigaction(SIGTERM, &oldTERM_sa, NULL);

  //Close ApolloSM and END
  if(NULL != SM) {delete SM;}

  return 0;
}
