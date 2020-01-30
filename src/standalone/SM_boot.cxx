#include <stdio.h>
#include <ApolloSM/ApolloSM.hh>
#include <ApolloSM/ApolloSM_Exceptions.hh>
#include <uhal/uhal.hpp>
#include <vector>
#include <string>
#include <boost/tokenizer.hpp>
#include <unistd.h> // usleep, execl
#include <signal.h>
#include <time.h>

#include <sys/stat.h> //for umask
#include <sys/types.h> //for umask

#include <BUException/ExceptionBase.hh>

#include <boost/program_options.hpp>
#include <fstream>

#include <tclap/CmdLine.h> //TCLAP parser

#include <syslog.h>  ///for syslog

#define SEC_IN_US  1000000
#define NS_IN_US 1000


#define DEFAULT_POLLTIME_IN_SECONDS 10
#define DEFAULT_CONFIG_FILE "/etc/SM_boot"
#define DEFAULT_RUN_DIR     "/opt/address_tables/"
#define DEFAULT_PID_FILE    "/var/run/sm_boot.pid"
#define DEFAULT_POWERUP_TIME 5

#define LOCK_FILE "sm_boot.lock"

// ====================================================================================================
// signal handling
bool static volatile loop;
void static signal_handler(int const signum) {
  if(SIGINT == signum || SIGTERM == signum) {
    loop = false;
  }
}



// ====================================================================================================
// Read from config files and set up all parameters
// For further information see https://theboostcpplibraries.com/boost.program_options

boost::program_options::variables_map loadConfig(std::string const & configFileName,
						 boost::program_options::options_description const & fileOptions) {
  // This is a container for the information that fileOptions will get from the config file
  boost::program_options::variables_map vm;  

  // Check if config file exists
  std::ifstream ifs{configFileName};
  syslog(LOG_INFO, "Config file \"%s\" %s\n",configFileName.c_str(), (!ifs.fail()) ? "exists" : "does not exist");

  if(ifs) {
    // If config file exists, parse ifs into fileOptions and store information from fileOptions into vm
    boost::program_options::store(parse_config_file(ifs, fileOptions), vm);
  }

  return vm;
}

// ====================================================================================================
long us_difftime(struct timespec cur, struct timespec end){ 
  return ( (end.tv_sec  - cur.tv_sec )*SEC_IN_US + 
	   (end.tv_nsec - cur.tv_nsec)/NS_IN_US);
}


// ====================================================================================================
// Definitions

typedef boost::tokenizer<boost::char_separator<char> > tokenizer;

struct temperatures {
  uint8_t MCUTemp;
  uint8_t FIREFLYTemp;
  uint8_t FPGATemp;
  uint8_t REGTemp;
  bool    validData;
};
// ====================================================================================================
temperatures sendAndParse(ApolloSM* SM) {
  temperatures temps {0,0,0,0,false};
  std::string recv;

  // read and print
  try{
    recv = (SM->UART_CMD("/dev/ttyUL1", "simple_sensor", '%'));
    temps.validData = true;
  }catch(BUException::IO_ERROR &e){
    //ignore this     
  }
  
  if (temps.validData){
    // Separate by line
    boost::char_separator<char> lineSep("\r\n");
    tokenizer lineTokens{recv, lineSep};

    // One vector for each line 
    std::vector<std::vector<std::string> > allTokens;

    // Separate by spaces
    boost::char_separator<char> space(" ");
    int vecCount = 0;
    // For each line
    for(tokenizer::iterator lineIt = lineTokens.begin(); lineIt != lineTokens.end(); ++lineIt) {
      tokenizer wordTokens{*lineIt, space};
      // We don't yet own any memory in allTokens so we append a blank vector
      std::vector<std::string> blankVec;
      allTokens.push_back(blankVec);
      // One vector per line
      for(tokenizer::iterator wordIt = wordTokens.begin(); wordIt != wordTokens.end(); ++wordIt) {
	allTokens[vecCount].push_back(*wordIt);
      }
      vecCount++;
    }

    // Check for at least one element 
    // Check for two elements in first element
    // Following lines follow the same concept
    std::vector<float> temp_values;
    for(size_t i = 0; 
	i < allTokens.size() && i < 4;
	i++){
      if(2 == allTokens[i].size()) {
	float temp;
	if( (temp = std::atof(allTokens[i][1].c_str())) < 0) {
	  temp = 0;
	}
	temp_values.push_back(temp);
      }
    }
    switch (temp_values.size()){
    case 4:
      temps.REGTemp = (uint8_t)temp_values[3];  
      //fallthrough
    case 3:
      temps.FPGATemp = (uint8_t)temp_values[2];  
      //fallthrough
    case 2:
      temps.FIREFLYTemp = (uint8_t)temp_values[1];  
      //fallthrough
    case 1:
      temps.MCUTemp = (uint8_t)temp_values[0];  
      //fallthrough
      break;
    default:
      break;
    }
  }
  return temps;
}

// ====================================================================================================
void updateTemp(ApolloSM * SM, std::string const & base,uint8_t temp){
  uint32_t oldValues = SM->RegReadRegister(base);
  oldValues = (oldValues & 0xFFFFFF00) | ((temp)&0x000000FF);
  if(0 == temp){    
    SM->RegWriteRegister(base,oldValues);
    return;
  }

  //Update max
  if(temp > (0xFF&(oldValues>>8))){
    oldValues = (oldValues & 0xFFFF00FF) | ((temp<< 8)&0x0000FF00);
  }
  //Update min
  if((temp < (0xFF&(oldValues>>16))) || 
     (0 == (0xFF&(oldValues>>16)))){
    oldValues = (oldValues & 0xFF00FFFF) | ((temp<<16)&0x00FF0000);
  }
  SM->RegWriteRegister(base,oldValues);
}

void sendTemps(ApolloSM* SM, temperatures temps) {
  updateTemp(SM,"SLAVE_I2C.S2.VAL", temps.MCUTemp);
  updateTemp(SM,"SLAVE_I2C.S3.VAL", temps.FIREFLYTemp);
  updateTemp(SM,"SLAVE_I2C.S4.VAL", temps.FPGATemp);
  updateTemp(SM,"SLAVE_I2C.S5.VAL", temps.REGTemp);
}


int main(int argc, char** argv) { 

  TCLAP::CmdLine cmd("ApolloSM boot interface");
  TCLAP::ValueArg<std::string> configFile("c",                 //one char flag
					  "config_file",       // full flag name
					  "config file",       //description
					  false,               //required argument
					  DEFAULT_CONFIG_FILE, //Default value
					  "string",            //type
					  cmd);
  TCLAP::ValueArg<std::string>    runPath    ("r","run_path","run path",false,DEFAULT_RUN_DIR ,"string",cmd);
  TCLAP::ValueArg<std::string>    pidFileName("p","pid_file","pid file",false,DEFAULT_PID_FILE,"string",cmd);

  try {
    cmd.parse(argc, argv);
  }catch (TCLAP::ArgException &e) {
    fprintf(stderr, "Failed to Parse Command Line\n");
    return -1;
  }



  // ============================================================================
  // Deamon book-keeping
  
  openlog(NULL,LOG_CONS|LOG_PID,LOG_DAEMON);

  // Open the lock file or create it if it does not
  int lockfd = open(LOCK_FILE, O_CREATE | O_RDWR, 0644); // Just O_RDONLY, 0444 would probably suffice.
  if(0 > lockfd) {
    syslog(LOG_ERR,"could not open lock file: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // lock the lock file
  if(0 > flock(lockfd, LOCK_EX | LOCK_NB)) {
    syslog(LOG_ERR,"could not lock lock file: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  } 
 
  // "w" will truncate (aka recreate) an existing pid file or create one if it does not exist 
  FILE * pidFile = fopen(pidFileName.getValue().c_str(),"w");

  // Make a child
  pid_t pid, sid;
  pid = fork();
  if(pid < 0){
    //Something went wrong.
    syslog(LOG_ERR,"could not fork a child: %s\n" strerror(errno));
    exit(EXIT_FAILURE);
  }else if(pid > 0){
    //We are the parent and created a child with pid pid
    fprintf(pidFile,"%d\n",pid);
    fclose(pidFile);
    exit(EXIT_SUCCESS);
  }else{
    // I'm the child!
    //open syslog
    //    openlog(NULL,LOG_CONS|LOG_PID,LOG_DAEMON);
  }

  // child here only
  // close pidfile
  fclose(pidFile);
  
  //Change the file mode mask to allow read/write
  umask(0);

  //Start logging
  syslog(LOG_INFO,"Opened log file\n");

  // create new SID for the daemon.
  sid = setsid();
  if (sid < 0) {
    syslog(LOG_ERR,"Failed to change SID\n");
    exit(EXIT_FAILURE);
  }
  syslog(LOG_INFO,"Set SID to %d\n",sid);

  //Move to RUN_DIR
  if ((chdir(runPath.getValue().c_str())) < 0) {
    syslog(LOG_ERR,"Failed to change path to \"%s\"\n",runPath.getValue().c_str());    
    exit(EXIT_FAILURE);
  }
  syslog(LOG_INFO,"Changed path to \"%s\"\n", runPath.getValue().c_str());    

  //Everything looks good, close the standard file fds.
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  
  // ============================================================================
  // Read from configuration file and set up parameters
  syslog(LOG_INFO,"Reading from config file now\n");
  int polltime_in_seconds = DEFAULT_POLLTIME_IN_SECONDS;
  bool powerupCMuC = true;
  int powerupTime = DEFAULT_POWERUP_TIME;
 
  // fileOptions is for parsing config files
  boost::program_options::options_description fileOptions{"File"};
  //sigh... with boost comes compilcated c++ magic
  fileOptions.add_options() 
    ("polltime", 
     boost::program_options::value<int>()->default_value(DEFAULT_POLLTIME_IN_SECONDS), 
     "polling interval")
    ("cm_powerup",
     boost::program_options::value<bool>()->default_value(true), 
     "power up CM uC")
    ("cm_powerup_time",
     boost::program_options::value<int>()->default_value(DEFAULT_POWERUP_TIME), 
     "uC powerup wait time");

  boost::program_options::variables_map configOptions;  
  try{
    configOptions = loadConfig(configFile.getValue(),fileOptions);
    // Check for information in configOptions
    if(configOptions.count("polltime")) {
      polltime_in_seconds = configOptions["polltime"].as<int>();
    }  
    syslog(LOG_INFO,
	   "Setting poll time to %d seconds (%s)\n",
	   polltime_in_seconds, 
	   configOptions.count("polltime") ? "CONFIG FILE" : "DEFAULT");
    if(configOptions.count("cm_powerup")) {
      powerupCMuC = configOptions["cm_powerup"].as<bool>();
    }  
    syslog(LOG_INFO,
	   "%s up CM uC @ boot (%s)\n",
	   powerupCMuC ? "Powering" : "Not powering",
	   configOptions.count("polltime") ? "CONFIG FILE" : "DEFAULT");
    if(configOptions.count("cm_powerup_time")) {
      powerupTime = configOptions["cm_powerup_time"].as<int>();
    }  
    syslog(LOG_INFO,
	   "Setting uC power-up time to %d seconds (%s)\n",
	   powerupTime,
	   configOptions.count("cm_powerup_time") ? "CONFIG FILE" : "DEFAULT");
        
  }catch(const boost::program_options::error &ex){
    syslog(LOG_INFO, "Caught exception in function loadConfig(): %s \n", ex.what());    
  }


  // ============================================================================
  // Daemon code setup

  // ====================================
  // Signal handling
  struct sigaction sa_INT,sa_TERM,old_sa;
  memset(&sa_INT ,0,sizeof(sa_INT)); //Clear struct
  memset(&sa_TERM,0,sizeof(sa_TERM)); //Clear struct
  //setup SA
  sa_INT.sa_handler  = signal_handler;
  sa_TERM.sa_handler = signal_handler;
  sigemptyset(&sa_INT.sa_mask);
  sigemptyset(&sa_TERM.sa_mask);
  sigaction(SIGINT,  &sa_INT , &old_sa);
  sigaction(SIGTERM, &sa_TERM, NULL);
  loop = true;

  // ====================================
  // for counting time
  struct timespec startTS;
  struct timespec stopTS;

  long update_period_us = polltime_in_seconds*SEC_IN_US; //sleep time in microseconds

  bool inShutdown = false;
  ApolloSM * SM = NULL;
  try{
    // ==================================
    // Initialize ApolloSM
    SM = new ApolloSM();
    if(NULL == SM){
      syslog(LOG_ERR,"Failed to create new ApolloSM\n");
      exit(EXIT_FAILURE);
    }else{
      syslog(LOG_INFO,"Created new ApolloSM\n");      
    }
    std::vector<std::string> arg;
    arg.push_back("connections.xml");
    SM->Connect(arg);
    //Set the power-up done bit to 1 for the IPMC to read
    SM->RegWriteRegister("SLAVE_I2C.S1.SM.STATUS.DONE",1);    
    syslog(LOG_INFO,"Set STATUS.DONE to 1\n");
  

    // ====================================
    // Turn on CM uC      
    if (powerupCMuC){
      SM->RegWriteRegister("CM.CM1.CTRL.ENABLE_UC",1);
      syslog(LOG_INFO,"Powering up CM uC\n");
      sleep(powerupTime);
    }
  

    // ==================================
    // Main DAEMON loop
    syslog(LOG_INFO,"Starting Monitoring loop\n");
    

    uint32_t CM_running = 0;
    while(loop) {
      // loop start time
      clock_gettime(CLOCK_REALTIME, &startTS);

      //=================================
      //Do work
      //=================================

      //Process CM temps
      temperatures temps;  
      if(SM->RegReadRegister("CM.CM1.CTRL.ENABLE_UC")){
	try{
	  temps = sendAndParse(SM);
	}catch(std::exception & e){
	  syslog(LOG_INFO,e.what());
	  //ignoring any exception here for now
	  temps = {0,0,0,0,false};
	}
	
	if(0 == CM_running ){
	  //Drop the non uC temps
	  temps.FIREFLYTemp = 0;
	  temps.FPGATemp = 0;
	  temps.REGTemp = 0;
	}
	CM_running = SM->RegReadRegister("CM.CM1.CTRL.PWR_GOOD");

	sendTemps(SM, temps);
	if(!temps.validData){
	  syslog(LOG_INFO,"Error in parsing data stream\n");
	}
      }else{
	temps = {0,0,0,0,false};
	sendTemps(SM, temps);
      }

      //Check if we are shutting down
      if((!inShutdown) && SM->RegReadRegister("SLAVE_I2C.S1.SM.STATUS.SHUTDOWN_REQ")){
	syslog(LOG_INFO,"Shutdown requested\n");
	inShutdown = true;
	//the IPMC requested a re-boot.
	pid_t reboot_pid;
	if(0 == (reboot_pid = fork())){
	  //Shutdown the system
	  execlp("/sbin/shutdown","/sbin/shutdown","-h","now",NULL);
	  exit(1);
	}
	if(-1 == reboot_pid){
	  inShutdown = false;
	  syslog(LOG_INFO,"Error! fork to shutdown failed!\n");
	}else{
	  //Shutdown the command module (if up)
	  SM->PowerDownCM(1,5);
	}
      }
      //=================================


      // monitoring sleep
      clock_gettime(CLOCK_REALTIME, &stopTS);
      // sleep for 10 seconds minus how long it took to read and send temperature    
      useconds_t sleep_us = update_period_us - us_difftime(startTS, stopTS);
      if(sleep_us > 0){
	usleep(sleep_us);
      }
    }
  }catch(BUException::exBase const & e){
    syslog(LOG_INFO,"Caught BUException: %s\n   Info: %s\n",e.what(),e.Description());
          
  }catch(std::exception const & e){
    syslog(LOG_INFO,"Caught std::exception: %s\n",e.what());
          
  }


  //make sure the CM is off
  //Shutdown the command module (if up)
  SM->PowerDownCM(1,5);
  SM->RegWriteRegister("CM.CM1.CTRL.ENABLE_UC",0);

  
  //If we are shutting down, do the handshanking.
  if(inShutdown){
    syslog(LOG_INFO,"Tell IPMC we have shut-down\n");
    //We are no longer booted
    SM->RegWriteRegister("SLAVE_I2C.S1.SM.STATUS.DONE",0);
    //we are shut down
    //    SM->RegWriteRegister("SLAVE_I2C.S1.SM.STATUS.SHUTDOWN",1);
    // one last HB
    //PS heartbeat
    SM->RegReadRegister("SLAVE_I2C.HB_SET1");
    SM->RegReadRegister("SLAVE_I2C.HB_SET2");

  }
  
  //Clean up
  if(NULL != SM) {
    delete SM;
  }
  
  // Restore old action of receiving SIGINT (which is to kill program) before returning 
  sigaction(SIGINT, &old_sa, NULL);
  syslog(LOG_INFO,"SM boot Daemon ended\n");
  
  // unlock lock file
  if(0 > flock(lockfd, LOCK_UN)) {
    syslog(LOG_ERR, "Cannot unlock lock file: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // If a process manages to open the lock file before it is unlinked, there may be a race condition
  // http://www.guido-flohr.net/never-delete-your-pid-file/

  // unlink lock file
  if(0 > unlink(LOCK_FILE)) {
    syslog(LOG_ERR, "Cannot unlink lock file: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  return 0;
}
